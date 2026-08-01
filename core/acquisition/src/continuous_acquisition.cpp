#include <vna/acquisition/continuous_acquisition.hpp>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace vna::acquisition {
namespace {

bool isFinite(const frames::ComplexSample& sample) {
    return std::isfinite(sample.real) && std::isfinite(sample.imaginary);
}
void validatePlan(const ContinuousAcquisitionPlan& plan) {
    const auto& axis = plan.frequencyAxis;
    if (axis.id.value() == 0 || axis.startFrequencyHz >= axis.stopFrequencyHz ||
        axis.points < 2 || axis.points > frames::kMaxSweepPoints ||
        plan.portCount == 0 || plan.portCount > frames::kMaxPortCount ||
        plan.sourcePorts.empty() ||
        plan.sourcePorts.size() > plan.portCount ||
        plan.minimumSweepPeriod < decltype(plan.minimumSweepPeriod)::zero()) {
        throw std::invalid_argument{"invalid continuous acquisition plan"};
    }
    std::vector<bool> seen(plan.portCount + 1, false);
    for (const auto port : plan.sourcePorts) {
        if (port == 0 || port > plan.portCount || seen[port]) {
            throw std::invalid_argument{"invalid continuous acquisition plan"};
        }
        seen[port] = true;
    }
}
std::optional<frames::FrameError> validatePayload(
    const ContinuousAcquisitionPlan& plan,
    const frames::RawReceiverPayload& payload) {
    if (payload.portCount != plan.portCount) {
        return frames::FrameError{frames::FrameErrorCode::InvalidPortCount};
    }
    if (payload.sourceStates.size() != plan.sourcePorts.size()) {
        return frames::FrameError{frames::FrameErrorCode::InvalidSourcePort};
    }
    for (std::size_t index = 0; index < payload.sourceStates.size(); ++index) {
        const auto& state = payload.sourceStates[index];
        if (state.sourcePort != plan.sourcePorts[index]) {
            return frames::FrameError{frames::FrameErrorCode::InvalidSourcePort};
        }
        if (state.samples.size() != plan.frequencyAxis.points) {
            return frames::FrameError{frames::FrameErrorCode::SampleCountMismatch};
        }
        for (const auto& sample : state.samples) {
            if (sample.responses.size() != plan.portCount) {
                return frames::FrameError{
                    frames::FrameErrorCode::ResponseCountMismatch};
            }
            const auto finiteResponses = std::all_of(
                sample.responses.cbegin(), sample.responses.cend(), isFinite);
            if (!isFinite(sample.reference) || !finiteResponses) {
                return frames::FrameError{frames::FrameErrorCode::NonFiniteSample};
            }
        }
    }
    return std::nullopt;
}

}  // namespace

class ContinuousAcquisition::Impl {
public:
    Impl(ContinuousAcquisitionPlan plan, RawSweepSource source)
        : plan_(std::move(plan)), source_(std::move(source)) {
        validatePlan(plan_);
        if (!source_) {
            throw std::invalid_argument{"continuous acquisition source is empty"};
        }
        worker_ = std::jthread{[this](std::stop_token token) { run(token); }};
    }

    ~Impl() {
        stop();
    }
    void stop() noexcept {
        worker_.request_stop();
        changed_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void join() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    RawFrameHandle latest() const {
        std::lock_guard lock{mutex_};
        return latest_;
    }

    RawFrameHandle waitForNext(
        std::uint64_t afterSequence,
        std::stop_token token) const {
        std::stop_callback notify{token, [this] { changed_.notify_all(); }};
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [&] {
            return token.stop_requested() ||
                   snapshot_.state != ContinuousAcquisitionState::Running ||
                   snapshot_.lastPublishedSequence > afterSequence;
        });
        if (token.stop_requested() ||
            snapshot_.lastPublishedSequence <= afterSequence) {
            return nullptr;
        }
        return latest_;
    }

    ContinuousAcquisitionSnapshot snapshot() const {
        std::lock_guard lock{mutex_};
        return snapshot_;
    }
private:
    void run(std::stop_token token) noexcept {
        std::uint64_t nextSequence = 1;
        while (!token.stop_requested()) {
            const auto startedAt = std::chrono::steady_clock::now();
            if (!acquireAndPublish(nextSequence, token)) {
                return;
            }
            ++nextSequence;
            if (!paceUntil(startedAt + plan_.minimumSweepPeriod, token)) {
                finishStopped();
                return;
            }
        }
        finishStopped();
    }
    bool acquireAndPublish(
        std::uint64_t sequence,
        std::stop_token token) noexcept {
        try {
            auto result = source_(plan_, sequence, token);
            if (token.stop_requested()) {
                finishStopped();
                return false;
            }
            if (!result.hasValue()) {
                fail(ContinuousAcquisitionFailureCode::SourceFailed,
                     sequence, result.error());
                return false;
            }
            if (const auto error = validatePayload(plan_, result.value())) {
                fail(ContinuousAcquisitionFailureCode::RawFrameRejected,
                     sequence, *error);
                return false;
            }
            auto frame = std::make_shared<const RawFrame>(RawFrame{
                .context = {FrameId{sequence}, SweepId{sequence}, sequence},
                .frequencyAxis = plan_.frequencyAxis,
                .payload = result.value(),
            });
            publish(std::move(frame), sequence);
            return true;
        } catch (...) {
            if (token.stop_requested()) {
                finishStopped();
            } else {
                fail(ContinuousAcquisitionFailureCode::UnexpectedFailure,
                     sequence, std::current_exception());
            }
            return false;
        }
    }
    void publish(RawFrameHandle frame, std::uint64_t sequence) {
        {
            std::lock_guard lock{mutex_};
            latest_ = std::move(frame);
            snapshot_.lastPublishedSequence = sequence;
        }
        changed_.notify_all();
    }
    bool paceUntil(
        std::chrono::steady_clock::time_point deadline,
        std::stop_token token) const {
        std::unique_lock lock{mutex_};
        changed_.wait_until(lock, deadline, [&] {
            return token.stop_requested();
        });
        return !token.stop_requested();
    }
    void finishStopped() noexcept {
        {
            std::lock_guard lock{mutex_};
            if (snapshot_.state == ContinuousAcquisitionState::Running) {
                snapshot_.state = ContinuousAcquisitionState::Stopped;
            }
        }
        changed_.notify_all();
    }
    void fail(
        ContinuousAcquisitionFailureCode code,
        std::uint64_t sequence,
        ContinuousAcquisitionFailureCause cause) noexcept {
        {
            std::lock_guard lock{mutex_};
            snapshot_.state = ContinuousAcquisitionState::Failed;
            snapshot_.failure = ContinuousAcquisitionFailure{
                code, sequence, std::move(cause)};
        }
        changed_.notify_all();
    }

    const ContinuousAcquisitionPlan plan_;
    const RawSweepSource source_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    RawFrameHandle latest_;
    ContinuousAcquisitionSnapshot snapshot_;
    std::jthread worker_;
};

ContinuousAcquisition::ContinuousAcquisition(
    ContinuousAcquisitionPlan plan,
    RawSweepSource source)
    : impl_(std::make_unique<Impl>(std::move(plan), std::move(source))) {}
ContinuousAcquisition::~ContinuousAcquisition() = default;
void ContinuousAcquisition::stop() noexcept {
    impl_->stop();
}

void ContinuousAcquisition::join() {
    impl_->join();
}
RawFrameHandle ContinuousAcquisition::latest() const {
    return impl_->latest();
}
RawFrameHandle ContinuousAcquisition::waitForNext(
    std::uint64_t afterSequence,
    std::stop_token token) const {
    return impl_->waitForNext(afterSequence, token);
}
ContinuousAcquisitionSnapshot ContinuousAcquisition::snapshot() const {
    return impl_->snapshot();
}
}  // namespace vna::acquisition
