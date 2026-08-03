#pragma once

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <variant>

#include <vna/acquisition/raw_sweep_capture.hpp>
#include <vna/application/operation_manager.hpp>
#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application {

struct StateSnapshot;

namespace detail {
struct PreparedSweepRuntimeConfigurationState;
}

namespace internal {
class SweepRuntimeImpl;
}

enum class SweepRuntimeConfigurationErrorCode {
    Stopped,
    Retired,
    Failed,
    UnsupportedSweepConfiguration,
    TraceConfigurationRejected,
};

struct SweepRuntimeConfigurationError {
    SweepRuntimeConfigurationErrorCode code;
};

// Preparation owns the short staging gate. Dropping this move-only value
// releases that gate without publishing a pending plan or changing generation.
// The creating SweepRuntime must outlive an unconsumed prepared value.
class PreparedSweepRuntimeConfiguration {
public:
    PreparedSweepRuntimeConfiguration(
        PreparedSweepRuntimeConfiguration&&) noexcept;
    PreparedSweepRuntimeConfiguration& operator=(
        PreparedSweepRuntimeConfiguration&&) noexcept;
    ~PreparedSweepRuntimeConfiguration();

    PreparedSweepRuntimeConfiguration(
        const PreparedSweepRuntimeConfiguration&) = delete;
    PreparedSweepRuntimeConfiguration& operator=(
        const PreparedSweepRuntimeConfiguration&) = delete;

private:
    explicit PreparedSweepRuntimeConfiguration(
        std::unique_ptr<detail::PreparedSweepRuntimeConfigurationState> state);

    std::unique_ptr<detail::PreparedSweepRuntimeConfigurationState> state_;
    friend class internal::SweepRuntimeImpl;
};

using SweepRuntimeConfigurationPrepareResult = std::variant<
    PreparedSweepRuntimeConfiguration,
    SweepRuntimeConfigurationError>;

struct SweepRuntimeExecutionPolicy {
    domain::SweepMode mode{domain::SweepMode::Continuous};
    std::uint32_t sweepCount{1};
};

struct SweepRuntimePlan {
    acquisition::ContinuousAcquisitionPlan acquisition;
    TracePublicationPlanHandle publication;
    std::uint32_t maximumPointsPerChunk;
    SweepRuntimeExecutionPolicy execution{};
};

enum class SweepRuntimeState {
    Running,
    Stopped,
    Retired,
    Failed,
};

enum class SweepRuntimePhase {
    Hold,
    Preparing,
    Sweeping,
    Publishing,
};

enum class SweepRuntimeRequestErrorCode {
    Stopped,
    Retired,
    Failed,
};

struct SweepRuntimeRequestError {
    SweepRuntimeRequestErrorCode code;
};

using SweepRuntimeRequestResult =
    std::variant<OperationId, SweepRuntimeRequestError>;

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
    SweepRuntimePhase phase{SweepRuntimePhase::Preparing};
    std::uint64_t configuredStateRevision{};
    // These identify the immutable plan used at the last Sweep boundary.
    // CommandBus stateRevision remains the separately observable configured
    // revision and may be newer while a Sweep is still in flight.
    std::uint64_t appliedStateRevision{};
    std::uint64_t appliedGeneration{1};
    std::uint64_t attemptedSweeps{};
    std::uint64_t completedSweeps{};
    std::uint64_t rejectedSweeps{};
    std::uint64_t previewRejectedSweeps{};
    // Historical diagnosis: a later success does not mean this is current.
    std::optional<SweepRuntimeFailure> lastSweepFailure;
    std::exception_ptr terminalFailure;
};

// The runtime is the sole owner of one capture source and starts one worker.
// Exchange, catalog, and operation manager are borrowed and must outlive it.
// Owner-serialized lifecycle calls must not race destruction or be called from
// the source/observer callbacks.
class SweepRuntime final {
public:
    SweepRuntime(
        SweepRuntimePlan plan,
        acquisition::RawSweepCaptureSource source,
        SweepPreviewExchange& previews,
        TracePublicationCatalog& catalog,
        OperationManager& operations);
    ~SweepRuntime();

    SweepRuntime(const SweepRuntime&) = delete;
    SweepRuntime& operator=(const SweepRuntime&) = delete;
    SweepRuntime(SweepRuntime&&) = delete;
    SweepRuntime& operator=(SweepRuntime&&) = delete;

    void stop() noexcept;
    void join();
    [[nodiscard]] SweepRuntimeConfigurationPrepareResult prepareConfiguration(
        const StateSnapshot& candidate);
    void commitConfiguration(
        PreparedSweepRuntimeConfiguration prepared) noexcept;
    [[nodiscard]] SweepRuntimeRequestResult requestRestart(
        OperationSubmission submission);
    [[nodiscard]] SweepRuntimeSnapshot snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna::application
