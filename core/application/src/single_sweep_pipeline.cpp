#include "single_sweep_pipeline_internal.hpp"

#include <cmath>
#include <utility>
#include <vector>

#include <vna/data_plane/log_magnitude_projector.hpp>
#include <vna/measurement/s11_synthesizer.hpp>

namespace vna::application::internal {
namespace {

using RawResult = std::variant<
    frames::RawReceiverFrame,
    OperationFailure>;
using FrequencyResult = std::variant<
    std::vector<double>,
    SingleSweepFailureCode>;

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

FrequencyResult materializeFrequencies(const frames::FrequencyAxis& axis) {
    std::vector<double> values;
    values.reserve(axis.points);
    const auto start = static_cast<long double>(axis.startFrequencyHz);
    const auto stop = static_cast<long double>(axis.stopFrequencyHz);
    const auto intervals = static_cast<long double>(axis.points - 1);
    for (std::uint32_t index = 0; index < axis.points; ++index) {
        // Force both endpoints, then interpolate in long double so rounding is
        // deferred until the final display value conversion.
        const auto exact = index == 0
                               ? start
                               : (index + 1 == axis.points
                                      ? stop
                                      : start + (stop - start) * index /
                                                    intervals);
        const auto frequency = static_cast<double>(exact);
        if (!std::isfinite(frequency) ||
            (!values.empty() && frequency <= values.back())) {
            return SingleSweepFailureCode::FrequencyMaterializationFailed;
        }
        values.push_back(frequency);
    }
    return values;
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
    if (const auto* error =
            std::get_if<SingleSweepFailureCode>(&frequencies)) {
        return OperationFailure{.code = *error};
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
        std::move(std::get<std::vector<double>>(frequencies)),
        values.value()};
}

}  // namespace vna::application::internal
