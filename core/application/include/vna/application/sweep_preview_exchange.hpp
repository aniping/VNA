#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vna/compat/stop_token.hpp>
#include <variant>
#include <vector>

#include <vna/acquisition/continuous_acquisition.hpp>
#include <vna/application/sweep_runtime_status.hpp>
#include <vna/application/trace_display_frame.hpp>

namespace vna::application {

namespace internal {
class SweepGenerationTransaction;
class SweepRuntimeImpl;
}

struct SweepPreviewIdentity {
    std::uint64_t generation;
    acquisition::SweepId sweepId;
    friend bool operator==(
        const SweepPreviewIdentity& left,
        const SweepPreviewIdentity& right) {
        return left.generation == right.generation &&
            left.sweepId == right.sweepId;
    }
    friend bool operator!=(
        const SweepPreviewIdentity& left,
        const SweepPreviewIdentity& right) {
        return !(left == right);
    }
};

// Each Trace carries a complete prefix from point zero. This lets a slow
// consumer skip intermediate events without reconstructing missed chunks.
struct SweepTracePreview {
    display_model::TraceId traceId;
    domain::MeasurementId measurementId;
    domain::MeasurementType measurementType;
    display_model::TraceFormat format;
    std::vector<double> frequenciesHz;
    TraceDisplaySamples samples;
};

struct SweepPreview {
    SweepPreviewIdentity identity;
    domain::ChannelId channelId;
    std::uint64_t stateRevision;
    std::uint64_t sequenceNumber;
    std::uint32_t totalPointCount;
    std::vector<SweepTracePreview> traces;
};

using SweepPreviewHandle = std::shared_ptr<const SweepPreview>;

enum class SweepPreviewErrorCode {
    InvalidIdentity,
    InvalidSequenceNumber,
    InvalidTotalPointCount,
    EmptyTraceSet,
    InvalidTraceIdentity,
    DuplicateTraceId,
    InvalidPrefixLength,
    SampleCountMismatch,
    NonFiniteValue,
    FrequencyNotStrictlyIncreasing,
    UnsupportedFormat,
    UnsupportedValueUnit,
    SamplePayloadMismatch,
    ProgressRegression,
    StaleGeneration,
    FutureGeneration,
    SweepIdRegression,
    GenerationNotNext,
};

struct SweepPreviewError {
    SweepPreviewErrorCode code;
};

using SweepPreviewPublishResult =
    std::variant<SweepPreviewHandle, SweepPreviewError>;

struct SweepPreviewCursor {
    std::uint64_t value{};
};

struct SweepPreviewStreamStatus {
    SweepRuntimeDisplayStatus runtime;
    std::optional<SweepPreviewIdentity> activePreviewIdentity;
};

struct SweepPreviewAvailable {
    SweepPreviewCursor cursor;
    SweepPreviewHandle preview;
    SweepPreviewStreamStatus status;
};

struct SweepPreviewInvalidated {
    SweepPreviewCursor cursor;
    SweepPreviewIdentity identity;
    SweepPreviewStreamStatus status;
};

struct SweepPreviewGenerationAdvanced {
    SweepPreviewCursor cursor;
    std::uint64_t generation;
    SweepPreviewStreamStatus status;
};

struct SweepPreviewStatusChanged {
    SweepPreviewCursor cursor;
    SweepPreviewStreamStatus status;
};

using SweepPreviewGenerationResult = std::variant<
    SweepPreviewGenerationAdvanced,
    SweepPreviewError>;

using SweepPreviewEvent = std::variant<
    SweepPreviewAvailable,
    SweepPreviewInvalidated,
    SweepPreviewGenerationAdvanced,
    SweepPreviewStatusChanged>;

class SweepPreviewExchange {
public:
    explicit SweepPreviewExchange(SweepRuntimeDisplayStatus initialStatus);

    // Every successful mutation advances one shared cursor. Waiters therefore
    // observe the newest Preview or invalidation without replaying history.
    // Methods are thread-safe; destruction requires all calls to have ended.
    [[nodiscard]] SweepPreviewPublishResult publish(SweepPreview preview);
    [[nodiscard]] SweepPreviewGenerationResult advanceGeneration(
        std::uint64_t nextGeneration);
    [[nodiscard]] bool invalidate(SweepPreviewIdentity identity) noexcept;
    [[nodiscard]] std::optional<SweepPreviewEvent> waitForNext(
        SweepPreviewCursor after,
        vna::compat::StopToken token = {}) const;

private:
    friend class internal::SweepGenerationTransaction;
    friend class internal::SweepRuntimeImpl;
    [[nodiscard]] SweepPreviewPublishResult publishForRuntime(
        SweepPreview preview,
        SweepRuntimeDisplayStatus status);
    [[nodiscard]] SweepPreviewPublishResult publishImpl(
        SweepPreview preview,
        const SweepRuntimeDisplayStatus* runtimeStatus);
    void updateForRuntime(SweepRuntimeDisplayStatus status) noexcept;
    [[nodiscard]] bool invalidateForRuntime(
        SweepPreviewIdentity identity,
        SweepRuntimeDisplayStatus status) noexcept;
    [[nodiscard]] bool matchesInitialStatus(
        const SweepRuntimeDisplayStatus& expected) const noexcept;
    [[nodiscard]] static bool validStatus(
        const SweepRuntimeDisplayStatus& status) noexcept;
    [[nodiscard]] static SweepPreviewStreamStatus streamStatus(
        const SweepRuntimeDisplayStatus& runtime,
        const SweepPreviewHandle& preview);
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::uint64_t generation_{1};
    std::uint64_t lastSweepId_{};
    std::uint64_t nextCursor_{1};
    SweepRuntimeDisplayStatus status_;
    std::optional<SweepPreviewIdentity> activeIdentity_;
    SweepPreviewHandle currentPreview_;
    std::optional<SweepPreviewEvent> latestEvent_;
};

}  // namespace vna::application
