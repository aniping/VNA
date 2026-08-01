#include <vna/measurement/s11_synthesizer.hpp>

#include <algorithm>
#include <optional>
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

struct SParameterPorts {
    std::uint32_t responsePort;
    std::uint32_t sourcePort;
};

std::optional<SParameterPorts> portsFor(domain::MeasurementType type) {
    switch (type) {
    case domain::MeasurementType::S11:
        return SParameterPorts{.responsePort = 1, .sourcePort = 1};
    case domain::MeasurementType::S21:
        return SParameterPorts{.responsePort = 2, .sourcePort = 1};
    }
    return std::nullopt;
}

std::optional<frames::FrameError> divideResponses(
    const frames::RawSourceState& source,
    std::uint32_t responsePort,
    std::vector<frames::ComplexSample>& ratios) {
    ratios.reserve(source.samples.size());
    for (const auto& sample : source.samples) {
        // A zero reference has no physical ratio. Reject the complete vector
        // rather than publish partial or non-finite measurement data.
        if (isZeroReference(sample.reference)) {
            return frames::FrameError{
                .code = frames::FrameErrorCode::ZeroReference};
        }
        ratios.push_back(divide(
            sample.responses[responsePort - 1], sample.reference));
    }
    return std::nullopt;
}

}  // namespace

frames::Result<frames::MeasurementFrame> synthesizeSParameter(
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
    const auto ports = portsFor(measurement.type);
    if (!ports) {
        return frames::Result<frames::MeasurementFrame>{frames::FrameError{
            .code = frames::FrameErrorCode::UnsupportedMeasurementType}};
    }
    if (measurement.channelId != validated.value().context.channelId) {
        return frames::Result<frames::MeasurementFrame>{frames::FrameError{
            .code = frames::FrameErrorCode::MeasurementChannelMismatch}};
    }

    if (ports->responsePort > validated.value().payload.portCount) {
        return frames::Result<frames::MeasurementFrame>{frames::FrameError{
            .code = frames::FrameErrorCode::InvalidPortCount}};
    }
    const auto& states = validated.value().payload.sourceStates;
    const auto source = std::find_if(
        states.cbegin(), states.cend(), [ports](const auto& state) {
            return state.sourcePort == ports->sourcePort;
        });
    if (source == states.cend()) {
        return frames::Result<frames::MeasurementFrame>{frames::FrameError{
            .code = frames::FrameErrorCode::InvalidSourcePort}};
    }

    std::vector<frames::ComplexSample> ratios;
    if (const auto error =
            divideResponses(*source, ports->responsePort, ratios)) {
        return frames::Result<frames::MeasurementFrame>{*error};
    }

    return frames::makeMeasurementFrame(
        validated.value().context,
        validated.value().frequencyAxis,
        measurement.id,
        measurement.type,
        std::move(ratios));
}

frames::Result<frames::MeasurementFrame> synthesizeS11(
    frames::RawReceiverFrame rawFrame,
    domain::MeasurementSnapshot measurement) {
    if (measurement.type != domain::MeasurementType::S11) {
        return frames::Result<frames::MeasurementFrame>{frames::FrameError{
            .code = frames::FrameErrorCode::UnsupportedMeasurementType}};
    }
    return synthesizeSParameter(std::move(rawFrame), measurement);
}

}  // namespace vna::measurement
