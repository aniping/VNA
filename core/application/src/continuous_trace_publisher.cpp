#include <vna/application/continuous_trace_publisher.hpp>

#include "continuous_trace_pipeline_internal.hpp"

#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <variant>

#include <spdlog/spdlog.h>

namespace vna::application {
namespace {

const char* failureCode(
    acquisition::ContinuousAcquisitionFailureCode code) noexcept {
    using acquisition::ContinuousAcquisitionFailureCode;
    switch (code) {
    case ContinuousAcquisitionFailureCode::SourceFailed:
        return "source-failed";
    case ContinuousAcquisitionFailureCode::RawFrameRejected:
        return "raw-frame-rejected";
    case ContinuousAcquisitionFailureCode::UnexpectedFailure:
        return "unexpected-failure";
    }
    return "unknown";
}

void logFirstPublishedSet(
    const acquisition::RawFrame& raw,
    const TracePublicationPlan& plan,
    std::size_t traceCount) noexcept {
    try {
        if (const auto logger = spdlog::get("vna")) {
            logger->debug(
                "[连续扫频] 已发布配置代次首个完整显示帧 | generation={} "
                "| revision={} | frame_id={} | sweep_id={} | sequence={} "
                "| trace_count={}",
                plan.generation, plan.stateRevision,
                raw.context.frameId.value(), raw.context.sweepId.value(),
                raw.context.sequenceNumber, traceCount);
        }
    } catch (...) {}
}

void logAcquisitionFailure(
    const acquisition::ContinuousAcquisitionFailure& failure) noexcept {
    try {
        if (const auto logger = spdlog::get("vna")) {
            logger->error(
                "[连续扫频] 持续采集已停止 | attempted_sequence={} | "
                "error_code={}",
                failure.attemptedSequence, failureCode(failure.code));
        }
    } catch (...) {}
}

}  // namespace

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
                if (acquisition.failure.has_value()) {
                    logAcquisitionFailure(*acquisition.failure);
                }
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
            recordPublished(raw.context.sequenceNumber);
            // Mark before best-effort logging so a broken sink cannot create a
            // 10 Hz retry storm for the same immutable configuration.
            if (logAttemptedGeneration_ != plan->generation) {
                logAttemptedGeneration_ = plan->generation;
                logFirstPublishedSet(raw, *plan, plan->targets.size());
            }
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

    void recordPublished(std::uint64_t sequence) noexcept {
        std::lock_guard lock{mutex_};
        ++snapshot_.publishedFrames;
        snapshot_.lastPublishedSequence = sequence;
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
    std::optional<std::uint64_t> logAttemptedGeneration_;
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
