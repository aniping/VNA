#include <vna/application/continuous_trace_publisher.hpp>

#include "frequency_axis_materialization_internal.hpp"

#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

#include <vna/data_plane/log_magnitude_projector.hpp>
#include <vna/measurement/s_parameter_synthesizer.hpp>

namespace vna::application {
namespace {
void validatePreset(const ContinuousTracePreset& preset) {
    if (preset.measurement.type != domain::MeasurementType::S21 ||
        preset.trace.format != display_model::TraceFormat::LogMagnitude ||
        preset.trace.measurementId != preset.measurement.id) {
        throw std::invalid_argument{"invalid continuous trace preset"};
    }
}

std::optional<TraceDisplayFrame> buildDisplayFrame(
    const acquisition::RawFrame& raw,
    const ContinuousTracePreset& preset) {
    // Acquisition publishes immutable frames. This first version makes one
    // explicit bounded copy so downstream validation can own and move payload.
    frames::RawReceiverFrame input{
        .context = {
            frames::FrameId{raw.context.frameId.value()},
            frames::SweepId{raw.context.sweepId.value()},
            preset.measurement.channelId,
            preset.stateRevision,
            raw.context.sequenceNumber},
        .frequencyAxis = raw.frequencyAxis,
        .payload = raw.payload,
    };
    auto measured = measurement::synthesizeSParameter(
        std::move(input), preset.measurement);
    if (!measured.hasValue()) {
        return std::nullopt;
    }
    auto values = data_plane::projectLogMagnitude(measured.value());
    auto frequencies = internal::materializeFrequencies(raw.frequencyAxis);
    if (!values.hasValue() || !frequencies.has_value()) {
        return std::nullopt;
    }
    // frames::Result intentionally exposes const values; this bounded copy is
    // explicit rather than disguised as a move from const storage.
    return TraceDisplayFrame{
        frames::FrameId{raw.context.frameId.value()},
        preset.trace.id,
        preset.stateRevision,
        raw.context.sequenceNumber,
        display_model::TraceFormat::LogMagnitude,
        display_model::ScaleUnit::Decibel,
        std::move(frequencies.value()),
        values.value()};
}

}  // namespace

class ContinuousTracePublisher::Impl {
public:
    Impl(
        acquisition::ContinuousAcquisition& acquisition,
        ContinuousTracePreset preset,
        TraceDisplayFrameRepository& repository,
        TraceDisplayPublisher publish)
        : acquisition_(acquisition), preset_(std::move(preset)),
          repository_(repository), publish_(std::move(publish)) {
        validatePreset(preset_);
        if (!publish_) {
            throw std::invalid_argument{"continuous trace publisher is empty"};
        }
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
    void invalidateTraceFrame(display_model::TraceId traceId) noexcept {
        if (traceId != preset_.trace.id) {
            return;
        }
        const std::lock_guard gate{publishGate_};
        repository_.discard(traceId);
    }
    void retireTrace(display_model::TraceId traceId) noexcept {
        if (traceId != preset_.trace.id) {
            return;
        }
        worker_.request_stop();
        const std::lock_guard gate{publishGate_};
        {
            std::lock_guard lock{mutex_};
            snapshot_.state = ContinuousTracePublisherState::Retired;
        }
        repository_.discard(traceId);
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
                process(*raw);
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
    void process(const acquisition::RawFrame& raw) noexcept {
        try {
            auto frame = buildDisplayFrame(raw, preset_);
            if (!frame.has_value()) {
                reject();
                return;
            }
            const std::lock_guard gate{publishGate_};
            if (isRetired()) {
                return;
            }
            if (!publish_(std::move(frame.value())).hasValue()) {
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
    bool isRetired() const {
        std::lock_guard lock{mutex_};
        return snapshot_.state == ContinuousTracePublisherState::Retired;
    }
    void finish(
        ContinuousTracePublisherState state,
        std::optional<acquisition::ContinuousAcquisitionFailure> failure = {}) {
        std::lock_guard lock{mutex_};
        if (snapshot_.state != ContinuousTracePublisherState::Retired) {
            snapshot_.state = state;
            snapshot_.acquisitionFailure = std::move(failure);
        }
    }
    acquisition::ContinuousAcquisition& acquisition_;
    const ContinuousTracePreset preset_;
    TraceDisplayFrameRepository& repository_;
    const TraceDisplayPublisher publish_;
    mutable std::mutex publishGate_;
    mutable std::mutex mutex_;
    ContinuousTracePublisherSnapshot snapshot_;
    std::jthread worker_;
};

ContinuousTracePublisher::ContinuousTracePublisher(
    acquisition::ContinuousAcquisition& acquisition,
    ContinuousTracePreset preset,
    TraceDisplayFrameRepository& repository)
    : ContinuousTracePublisher(
          acquisition,
          std::move(preset),
          repository,
          [&repository](TraceDisplayFrame frame) {
              return repository.publish(std::move(frame));
          }) {}
ContinuousTracePublisher::ContinuousTracePublisher(
    acquisition::ContinuousAcquisition& acquisition,
    ContinuousTracePreset preset,
    TraceDisplayFrameRepository& repository,
    TraceDisplayPublisher publish)
    : impl_(std::make_unique<Impl>(
          acquisition, std::move(preset), repository, std::move(publish))) {}
ContinuousTracePublisher::~ContinuousTracePublisher() = default;
void ContinuousTracePublisher::stop() noexcept { impl_->stop(); }
void ContinuousTracePublisher::join() { impl_->join(); }
void ContinuousTracePublisher::invalidateTraceFrame(
    display_model::TraceId traceId) noexcept {
    impl_->invalidateTraceFrame(traceId);
}
void ContinuousTracePublisher::retireTrace(
    display_model::TraceId traceId) noexcept {
    impl_->retireTrace(traceId);
}
ContinuousTracePublisherSnapshot ContinuousTracePublisher::snapshot() const {
    return impl_->snapshot();
}
}  // namespace vna::application
