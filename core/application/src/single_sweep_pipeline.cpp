#include "single_sweep_pipeline_internal.hpp"
#include "frequency_axis_materialization_internal.hpp"

#include <utility>

#include <vna/data_plane/log_magnitude_projector.hpp>
#include <vna/measurement/s11_synthesizer.hpp>

namespace vna::application::internal {
namespace {

using RawResult = std::variant<
    frames::RawReceiverFrame,
    OperationFailure>;

RawResult acquireRaw(
    const SingleSweepWorkItem& work,
    const RawSweepSource& source,
    std::stop_token token) {
    try {
        auto payload = source(work.frequencyAxis, token);
        if (!payload.hasValue()) {
            return OperationFailure{
                .code = SingleSweepFailureCode::RawSweepFailed,
                .cause = payload.error()};
        }
        auto raw = frames::makeRawReceiverFrame(
            work.frameContext, work.frequencyAxis, payload.value());
        if (!raw.hasValue()) {
            return OperationFailure{
                .code = SingleSweepFailureCode::RawFrameRejected,
                .cause = raw.error()};
        }
        return raw.value();
    } catch (...) {
        // An adapter exception cannot escape the sole worker and strand later
        // queue entries; it is normalized at the acquisition boundary.
        return OperationFailure{
            .code = SingleSweepFailureCode::RawSweepFailed,
            .cause = std::current_exception()};
    }
}

}  // namespace

SweepPipelineResult buildSingleSweepFrame(
    const SingleSweepWorkItem& work,
    const RawSweepSource& source,
    std::stop_token token,
    const SweepCancellationCheck& canceled) {
    if (canceled()) {
        return SweepPipelineCanceled{};
    }
    auto raw = acquireRaw(work, source, token);
    if (const auto* error = std::get_if<OperationFailure>(&raw)) {
        return *error;
    }
    if (canceled()) {
        return SweepPipelineCanceled{};
    }
    auto measured = measurement::synthesizeS11(
        std::get<frames::RawReceiverFrame>(raw), work.measurement);
    if (!measured.hasValue()) {
        return OperationFailure{
            .code = SingleSweepFailureCode::MeasurementSynthesisFailed,
            .cause = measured.error()};
    }
    if (canceled()) {
        return SweepPipelineCanceled{};
    }
    auto values = data_plane::projectLogMagnitude(measured.value());
    if (!values.hasValue()) {
        return OperationFailure{
            .code = SingleSweepFailureCode::LogMagnitudeProjectionFailed,
            .cause = values.error()};
    }
    auto frequencies = materializeFrequencies(work.frequencyAxis);
    if (!frequencies.has_value()) {
        return OperationFailure{
            .code = SingleSweepFailureCode::FrequencyMaterializationFailed};
    }
    if (canceled()) {
        return SweepPipelineCanceled{};
    }
    return TraceDisplayFrame{
        work.frameContext.frameId,
        work.traceId,
        work.frameContext.stateRevision,
        work.frameContext.sequenceNumber,
        display_model::TraceFormat::LogMagnitude,
        display_model::ScaleUnit::Decibel,
        std::move(frequencies.value()),
        values.value()};
}

}  // namespace vna::application::internal
