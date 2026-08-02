#pragma once

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <variant>

#include <vna/acquisition/raw_sweep_capture.hpp>
#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application {

struct SweepRuntimePlan {
    acquisition::ContinuousAcquisitionPlan acquisition;
    TracePublicationPlanHandle publication;
    std::uint32_t maximumPointsPerChunk;
};

enum class SweepRuntimeState {
    Running,
    Stopped,
    Retired,
    Failed,
};

enum class SweepRuntimeFailureCode {
    CaptureFailed,
    CompleteProcessingFailed,
    PublicationRejected,
};

using SweepRuntimeFailureCause = std::variant<
    std::monostate,
    frames::FrameError,
    TracePublicationCatalogError>;

struct SweepRuntimeFailure {
    SweepRuntimeFailureCode code;
    std::uint64_t attemptedSequence;
    SweepRuntimeFailureCause cause;
};

struct SweepRuntimeSnapshot {
    SweepRuntimeState state{SweepRuntimeState::Running};
    std::uint64_t attemptedSweeps{};
    std::uint64_t completedSweeps{};
    std::uint64_t rejectedSweeps{};
    std::uint64_t previewRejectedSweeps{};
    // Historical diagnosis: a later success does not mean this is current.
    std::optional<SweepRuntimeFailure> lastSweepFailure;
    std::exception_ptr terminalFailure;
};

// The runtime is the sole owner of one capture source and starts one worker.
// Exchange and catalog are borrowed and must outlive it. Owner-serialized
// lifecycle calls must not race destruction or be called from source/observer.
class SweepRuntime final {
public:
    SweepRuntime(
        SweepRuntimePlan plan,
        acquisition::RawSweepCaptureSource source,
        SweepPreviewExchange& previews,
        TracePublicationCatalog& catalog);
    ~SweepRuntime();

    SweepRuntime(const SweepRuntime&) = delete;
    SweepRuntime& operator=(const SweepRuntime&) = delete;
    SweepRuntime(SweepRuntime&&) = delete;
    SweepRuntime& operator=(SweepRuntime&&) = delete;

    void stop() noexcept;
    void join();
    [[nodiscard]] SweepRuntimeSnapshot snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna::application
