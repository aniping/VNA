#include "continuous_trace_pipeline_internal.hpp"

#include "frequency_axis_materialization_internal.hpp"

#include <cstddef>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vna/data_plane/trace_projector.hpp>
#include <vna/measurement/s_parameter_synthesizer.hpp>

namespace vna::application::internal {
namespace {

struct MeasurementBatch {
    std::vector<domain::MeasurementSnapshot> requests;
    std::unordered_map<std::uint64_t, std::size_t> indexById;
};

frames::RawReceiverFrame copyRawFrame(
    const acquisition::RawFrame& raw,
    const TracePublicationPlan& plan) {
    // This is the sole bounded RawFrame payload copy at the application edge;
    // every measurement and Trace below reuses this receiver frame.
    return {
        .context = {
            frames::FrameId{raw.context.frameId.value()},
            frames::SweepId{raw.context.sweepId.value()},
            plan.channelId,
            plan.stateRevision,
            raw.context.sequenceNumber},
        .frequencyAxis = raw.frequencyAxis,
        .payload = raw.payload,
    };
}

MeasurementBatch collectMeasurements(
    const std::vector<TracePublicationTarget>& targets) {
    MeasurementBatch batch;
    batch.requests.reserve(targets.size());
    batch.indexById.reserve(targets.size());
    for (const auto& target : targets) {
        const auto id = target.measurement.id.value();
        if (batch.indexById.contains(id)) {
            continue;
        }
        batch.indexById.emplace(id, batch.requests.size());
        batch.requests.push_back(target.measurement);
    }
    return batch;
}

std::optional<TraceDisplaySamples> convertSamples(
    const data_plane::ProjectedTraceSamples& projected) {
    return std::visit(
        [](const auto& samples) -> std::optional<TraceDisplaySamples> {
            using Samples = std::decay_t<decltype(samples)>;
            if constexpr (std::is_same_v<
                              Samples, data_plane::ScalarTraceSamples>) {
                if (samples.unit == data_plane::ProjectedTraceUnit::Decibel) {
                    return CartesianTraceDisplaySamples{
                        TraceDisplayUnit::Decibel, samples.values};
                }
                if (samples.unit == data_plane::ProjectedTraceUnit::Degree) {
                    return CartesianTraceDisplaySamples{
                        TraceDisplayUnit::Degree, samples.values};
                }
            } else if (samples.unit ==
                       data_plane::ProjectedTraceUnit::Unitless) {
                return ComplexTraceDisplaySamples{
                    TraceDisplayUnit::Unitless, samples.values};
            }
            return std::nullopt;
        },
        projected);
}

std::optional<TraceDisplayFrame> projectTarget(
    const acquisition::RawFrame& raw,
    const TracePublicationPlan& plan,
    const TracePublicationTarget& target,
    const frames::MeasurementFrame& measurement,
    const std::vector<double>& frequencies) {
    const auto projected =
        data_plane::projectTraceSamples(measurement, target.trace.format);
    if (!projected.hasValue()) {
        return std::nullopt;
    }
    auto samples = convertSamples(projected.value());
    if (!samples.has_value()) {
        return std::nullopt;
    }
    return TraceDisplayFrame{
        .frameId = frames::FrameId{raw.context.frameId.value()},
        .traceId = target.trace.id,
        .measurementId = target.measurement.id,
        .measurementType = target.measurement.type,
        .stateRevision = plan.stateRevision,
        .generation = plan.generation,
        .sequenceNumber = raw.context.sequenceNumber,
        .format = target.trace.format,
        .frequenciesHz = frequencies,
        .samples = std::move(*samples),
    };
}

}  // namespace

std::optional<TraceDisplayFrameSet> buildTraceDisplayFrameSet(
    const acquisition::RawFrame& raw,
    const TracePublicationPlan& plan) {
    auto frequencies = materializeFrequencies(raw.frequencyAxis);
    if (!frequencies.has_value()) {
        return std::nullopt;
    }
    const auto input = copyRawFrame(raw, plan);
    const auto batch = collectMeasurements(plan.targets);
    const auto measured =
        measurement::synthesizeSParameters(input, batch.requests);
    if (!measured.hasValue()) {
        return std::nullopt;
    }
    std::vector<TraceDisplayFrame> frames;
    frames.reserve(plan.targets.size());
    for (const auto& target : plan.targets) {
        const auto found = batch.indexById.find(target.measurement.id.value());
        if (found == batch.indexById.cend()) {
            return std::nullopt;
        }
        auto frame = projectTarget(
            raw, plan, target, measured.value()[found->second], *frequencies);
        if (!frame.has_value()) {
            return std::nullopt;
        }
        frames.push_back(std::move(*frame));
    }
    return TraceDisplayFrameSet{
        .generation = plan.generation,
        .sequenceNumber = raw.context.sequenceNumber,
        .frames = std::move(frames),
    };
}

}  // namespace vna::application::internal
