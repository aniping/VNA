#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

#include <vna/acquisition/raw_sweep_capture.hpp>
#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application {

struct SweepPreviewAssemblyPlan {
    acquisition::ContinuousAcquisitionPlan acquisition;
    TracePublicationPlanHandle publication;
    acquisition::SweepId sweepId;
    std::uint64_t sequenceNumber;
};

struct SweepPreviewAssemblyPending {};

enum class SweepPreviewAssemblyErrorCode {
    UnexpectedSourcePort,
    RangeGap,
    RangeOverlap,
    MeasurementSynthesisFailed,
    TraceProjectionFailed,
    SamplePayloadMismatch,
};

struct SweepPreviewAssemblyError {
    SweepPreviewAssemblyErrorCode code;
    std::optional<frames::FrameError> cause;
};

using SweepPreviewAssemblyResult = std::variant<
    SweepPreview,
    SweepPreviewAssemblyPending,
    SweepPreviewAssemblyError>;

// This module owns only one Sweep's temporary raw progress. Every successful
// append returns a self-contained prefix; no caller reconstructs missed chunks.
class SweepPreviewAssembler final {
public:
    explicit SweepPreviewAssembler(SweepPreviewAssemblyPlan plan);
    ~SweepPreviewAssembler();
    SweepPreviewAssembler(const SweepPreviewAssembler&) = delete;
    SweepPreviewAssembler& operator=(const SweepPreviewAssembler&) = delete;
    SweepPreviewAssembler(SweepPreviewAssembler&&) = delete;
    SweepPreviewAssembler& operator=(SweepPreviewAssembler&&) = delete;

    [[nodiscard]] SweepPreviewAssemblyResult append(
        const acquisition::RawSweepPointRange& range);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna::application
