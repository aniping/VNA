#include <vna/measurement/s11_synthesizer.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace vna::measurement {
namespace {

frames::ComplexSample divide(
    const frames::ComplexSample& numerator,
    const frames::ComplexSample& denominator) {
    const auto magnitudeSquared = denominator.real * denominator.real +
                                  denominator.imaginary * denominator.imaginary;
    return frames::ComplexSample{
        .real = (numerator.real * denominator.real +
                 numerator.imaginary * denominator.imaginary) /
                magnitudeSquared,
        .imaginary = (numerator.imaginary * denominator.real -
                      numerator.real * denominator.imaginary) /
                     magnitudeSquared,
    };
}

bool isZeroReference(const frames::ComplexSample& reference) {
    return reference.real * reference.real +
               reference.imaginary * reference.imaginary ==
           0.0;
}

bool isFirstSource(const frames::RawSourceState& state) {
    return state.sourcePort == 1;
}

}  // namespace

frames::Result<frames::MeasurementFrame> synthesizeS11(
    frames::RawReceiverFrame rawFrame,
    domain::MeasurementSnapshot measurement) {
    // Re-enter the public frame factory before doing arithmetic. Raw frame
    // structs remain aggregate-friendly at adapter boundaries, so synthesis
    // must not assume a caller bypassing the factory supplied valid samples.
    auto validated = frames::makeRawReceiverFrame(
        rawFrame.context,
        rawFrame.frequencyAxis,
        std::move(rawFrame.payload));
    if (!validated.hasValue()) {
        return frames::Result<frames::MeasurementFrame>{validated.error()};
    }
    if (measurement.type != domain::MeasurementType::S11) {
        return frames::Result<frames::MeasurementFrame>{frames::FrameError{
            .code = frames::FrameErrorCode::UnsupportedMeasurementType}};
    }
    if (measurement.channelId != validated.value().context.channelId) {
        return frames::Result<frames::MeasurementFrame>{frames::FrameError{
            .code = frames::FrameErrorCode::MeasurementChannelMismatch}};
    }

    const auto& states = validated.value().payload.sourceStates;
    const auto source =
        std::find_if(states.cbegin(), states.cend(), isFirstSource);
    if (source == states.cend()) {
        return frames::Result<frames::MeasurementFrame>{frames::FrameError{
            .code = frames::FrameErrorCode::InvalidSourcePort}};
    }

    std::vector<frames::ComplexSample> ratios;
    ratios.reserve(source->samples.size());
    for (const auto& sample : source->samples) {
        // A zero reference has no physical ratio. Reject the complete frame
        // instead of publishing a partial vector or allowing NaN/Inf to carry
        // an acquisition failure into later display processing.
        if (isZeroReference(sample.reference)) {
            return frames::Result<frames::MeasurementFrame>{frames::FrameError{
                .code = frames::FrameErrorCode::ZeroReference}};
        }
        ratios.push_back(divide(sample.responses[0], sample.reference));
    }

    return frames::makeMeasurementFrame(
        validated.value().context,
        validated.value().frequencyAxis,
        measurement.id,
        measurement.type,
        std::move(ratios));
}

}  // namespace vna::measurement
