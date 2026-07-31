#pragma once

namespace vna::application {

// Long-running work fails after command acceptance, so its terminal reason is
// an application operation concern rather than a CommandError. The code names
// the pipeline stage while lower-layer details remain at their module boundary.
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
};

}  // namespace vna::application
