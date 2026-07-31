#include <vna/measurement/s11_synthesizer.hpp>

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

    std::vector<frames::ComplexSample> ratios;
    ratios.reserve(validated.value().payload.samples.size());
    for (const auto& sample : validated.value().payload.samples) {
        // A zero reference has no physical ratio. Reject the complete frame
        // instead of publishing a partial vector or allowing NaN/Inf to carry
        // an acquisition failure into later display processing.
        if (isZeroReference(sample.a1)) {
            return frames::Result<frames::MeasurementFrame>{frames::FrameError{
                .code = frames::FrameErrorCode::ZeroReference}};
        }
        ratios.push_back(divide(sample.b1, sample.a1));
    }

    return frames::makeMeasurementFrame(
        validated.value().context,
        validated.value().frequencyAxis,
        measurement.id,
        measurement.type,
        std::move(ratios));
}

}  // namespace vna::measurement
