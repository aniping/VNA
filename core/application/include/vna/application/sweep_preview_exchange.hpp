#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <variant>
#include <vector>

#include <vna/acquisition/continuous_acquisition.hpp>
#include <vna/application/trace_display_frame.hpp>

namespace vna::application {

namespace internal {
class SweepGenerationTransaction;
}

struct SweepPreviewIdentity {
    std::uint64_t generation;
    acquisition::SweepId sweepId;
    friend bool operator==(
        const SweepPreviewIdentity&,
        const SweepPreviewIdentity&) = default;
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

struct SweepPreviewAvailable {
    SweepPreviewCursor cursor;
    SweepPreviewHandle preview;
};

struct SweepPreviewInvalidated {
    SweepPreviewCursor cursor;
    SweepPreviewIdentity identity;
};

struct SweepPreviewGenerationAdvanced {
    SweepPreviewCursor cursor;
    std::uint64_t generation;
};

using SweepPreviewGenerationResult = std::variant<
    SweepPreviewGenerationAdvanced,
    SweepPreviewError>;

using SweepPreviewEvent = std::variant<
    SweepPreviewAvailable,
    SweepPreviewInvalidated,
    SweepPreviewGenerationAdvanced>;

class SweepPreviewExchange {
public:
    // Every successful mutation advances one shared cursor. Waiters therefore
    // observe the newest Preview or invalidation without replaying history.
    // Methods are thread-safe; destruction requires all calls to have ended.
    [[nodiscard]] SweepPreviewPublishResult publish(SweepPreview preview);
    [[nodiscard]] SweepPreviewGenerationResult advanceGeneration(
        std::uint64_t nextGeneration);
    [[nodiscard]] bool invalidate(SweepPreviewIdentity identity) noexcept;
    [[nodiscard]] std::optional<SweepPreviewEvent> waitForNext(
        SweepPreviewCursor after,
        std::stop_token token = {}) const;

private:
    friend class internal::SweepGenerationTransaction;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::uint64_t generation_{1};
    std::uint64_t lastSweepId_{};
    std::uint64_t nextCursor_{1};
    std::optional<SweepPreviewIdentity> activeIdentity_;
    SweepPreviewHandle currentPreview_;
    std::optional<SweepPreviewEvent> latestEvent_;
};

}  // namespace vna::application
