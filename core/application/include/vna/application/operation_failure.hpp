#pragma once

#include <exception>
#include <variant>

#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/frames/frames.hpp>

namespace vna::application {

// Long-running work fails after command acceptance, so its terminal reason is
// an application operation concern rather than a CommandError. The code names
// the pipeline stage while the typed cause preserves its originating module.
enum class SingleSweepFailureCode {
    RawSweepFailed,
    RawFrameRejected,
    MeasurementSynthesisFailed,
    LogMagnitudeProjectionFailed,
    FrequencyMaterializationFailed,
    TraceDisplayPublishFailed,
};

struct OperationFailure {
    SingleSweepFailureCode code;
    // The application code is stable for lifecycle consumers, while the cause
    // retains the exact lower-module failure for diagnostics and recovery.
    std::variant<
        std::monostate,
        frames::FrameError,
        TraceDisplayFrameError,
        std::exception_ptr> cause{};
};

}  // namespace vna::application
