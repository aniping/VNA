#include <vna/application/continuous_trace_publisher.hpp>

#include "continuous_trace_pipeline_internal.hpp"

#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <variant>

namespace vna::application {

class ContinuousTracePublisher::Impl {
public:
    Impl(
        acquisition::ContinuousAcquisition& acquisition,
        TracePublicationCatalog& catalog)
        : acquisition_(acquisition), catalog_(catalog) {
        worker_ = std::jthread{[this](std::stop_token token) { run(token); }};
    }

    ~Impl() { stop(); }

    void stop() noexcept {
        worker_.request_stop();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void join() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    ContinuousTracePublisherSnapshot snapshot() const {
        std::lock_guard lock{mutex_};
        return snapshot_;
    }

private:
    void run(std::stop_token token) noexcept {
        std::uint64_t afterSequence = 0;
        while (!token.stop_requested()) {
            const auto raw = acquisition_.waitForNext(afterSequence, token);
            if (token.stop_requested()) {
                break;
            }
            if (raw != nullptr) {
                afterSequence = raw->context.sequenceNumber;
                observe(afterSequence);
                // The raw is already complete before capture, so a plan
                // committed while acquisition was blocked applies immediately.
                const auto plan = catalog_.capture();
                process(*raw, plan);
                continue;
            }
            const auto acquisition = acquisition_.snapshot();
            if (acquisition.state ==
                acquisition::ContinuousAcquisitionState::Failed) {
                finish(ContinuousTracePublisherState::AcquisitionFailed,
                       acquisition.failure);
                return;
            }
            if (acquisition.state ==
                acquisition::ContinuousAcquisitionState::Stopped) {
                finish(ContinuousTracePublisherState::Stopped);
                return;
            }
        }
        finish(ContinuousTracePublisherState::Stopped);
    }

    void process(
        const acquisition::RawFrame& raw,
        const TracePublicationPlanHandle& plan) noexcept {
        if (plan == nullptr || plan->targets.empty()) {
            return;
        }
        try {
            auto frameSet = internal::buildTraceDisplayFrameSet(raw, *plan);
            if (!frameSet.has_value()) {
                reject();
                return;
            }
            const auto published =
                catalog_.publishIfCurrent(plan, std::move(*frameSet));
            if (!std::holds_alternative<TraceDisplayFrameSetHandle>(published)) {
                reject();
                return;
            }
            std::lock_guard lock{mutex_};
            ++snapshot_.publishedFrames;
            snapshot_.lastPublishedSequence = raw.context.sequenceNumber;
        } catch (...) {
            reject();
        }
    }

    void observe(std::uint64_t sequence) {
        std::lock_guard lock{mutex_};
        ++snapshot_.observedFrames;
        snapshot_.lastObservedSequence = sequence;
    }

    void reject() noexcept {
        std::lock_guard lock{mutex_};
        ++snapshot_.rejectedFrames;
    }

    void finish(
        ContinuousTracePublisherState state,
        std::optional<acquisition::ContinuousAcquisitionFailure> failure = {}) {
        std::lock_guard lock{mutex_};
        snapshot_.state = state;
        snapshot_.acquisitionFailure = std::move(failure);
    }

    acquisition::ContinuousAcquisition& acquisition_;
    TracePublicationCatalog& catalog_;
    mutable std::mutex mutex_;
    ContinuousTracePublisherSnapshot snapshot_;
    std::jthread worker_;
};

ContinuousTracePublisher::ContinuousTracePublisher(
    acquisition::ContinuousAcquisition& acquisition,
    TracePublicationCatalog& catalog)
    : impl_(std::make_unique<Impl>(acquisition, catalog)) {}

ContinuousTracePublisher::~ContinuousTracePublisher() = default;
void ContinuousTracePublisher::stop() noexcept { impl_->stop(); }
void ContinuousTracePublisher::join() { impl_->join(); }
ContinuousTracePublisherSnapshot ContinuousTracePublisher::snapshot() const {
    return impl_->snapshot();
}

}  // namespace vna::application
