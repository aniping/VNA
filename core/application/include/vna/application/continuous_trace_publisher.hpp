#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <vna/acquisition/continuous_acquisition.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/display_model/display_workspace.hpp>
#include <vna/domain/instrument.hpp>

namespace vna::application {
struct ContinuousTracePreset {
    std::uint64_t stateRevision;
    domain::MeasurementSnapshot measurement;
    display_model::TraceSnapshot trace;
};

enum class ContinuousTracePublisherState {
    Running,
    Stopped,
    Retired,
    AcquisitionFailed,
};

struct ContinuousTracePublisherSnapshot {
    ContinuousTracePublisherState state{ContinuousTracePublisherState::Running};
    std::uint64_t observedFrames{};
    std::uint64_t publishedFrames{};
    std::uint64_t rejectedFrames{};
    std::uint64_t lastObservedSequence{};
    std::uint64_t lastPublishedSequence{};
    std::optional<acquisition::ContinuousAcquisitionFailure> acquisitionFailure;
};

// This worker is the sole production waitForNext consumer of acquisition state.
// Acquisition, repository, and injected publisher dependencies outlive it.
class ContinuousTracePublisher final {
public:
    ContinuousTracePublisher(
        acquisition::ContinuousAcquisition& acquisition,
        ContinuousTracePreset preset,
        TraceDisplayFrameRepository& repository);
    // The callback must return promptly and must not re-enter this publisher or
    // CommandBus; the lifecycle gate intentionally spans the complete call.
    ContinuousTracePublisher(
        acquisition::ContinuousAcquisition& acquisition,
        ContinuousTracePreset preset,
        TraceDisplayFrameRepository& repository,
        TraceDisplayPublisher publish);
    ~ContinuousTracePublisher();
    ContinuousTracePublisher(const ContinuousTracePublisher&) = delete;
    ContinuousTracePublisher& operator=(const ContinuousTracePublisher&) = delete;
    ContinuousTracePublisher(ContinuousTracePublisher&&) = delete;
    ContinuousTracePublisher& operator=(ContinuousTracePublisher&&) = delete;

    // Owner serializes stop, join, and destruction. stop wakes only this worker;
    // join blocks until acquisition reaches a natural terminal state.
    void stop() noexcept;
    void join();
    // Invalidation is linearized with publication but keeps this worker alive.
    void invalidateTraceFrame(display_model::TraceId traceId) noexcept;
    // Retirement is trace-scoped and linearized with the final publish. It
    // requests only this worker to stop and deliberately does not join it.
    void retireTrace(display_model::TraceId traceId) noexcept;
    [[nodiscard]] ContinuousTracePublisherSnapshot snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vna::application
