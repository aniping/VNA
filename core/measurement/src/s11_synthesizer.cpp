#include <vna/measurement/s11_synthesizer.hpp>

#include <algorithm>
#include <array>
#include <map>
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
    case domain::MeasurementType::S12:
        return SParameterPorts{.responsePort = 1, .sourcePort = 2};
    case domain::MeasurementType::S22:
        return SParameterPorts{.responsePort = 2, .sourcePort = 2};
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

std::optional<frames::FrameError> calculateRatios(
    const frames::RawReceiverFrame& rawFrame,
    domain::MeasurementType type,
    std::vector<frames::ComplexSample>& ratios) {
    const auto ports = portsFor(type);
    if (!ports) {
        return frames::FrameError{
            .code = frames::FrameErrorCode::UnsupportedMeasurementType};
    }
    if (ports->responsePort > rawFrame.payload.portCount) {
        return frames::FrameError{
            .code = frames::FrameErrorCode::InvalidPortCount};
    }
    const auto& states = rawFrame.payload.sourceStates;
    const auto source = std::find_if(
        states.cbegin(), states.cend(), [ports](const auto& state) {
            return state.sourcePort == ports->sourcePort;
        });
    if (source == states.cend()) {
        return frames::FrameError{
            .code = frames::FrameErrorCode::InvalidSourcePort};
    }
    return divideResponses(*source, ports->responsePort, ratios);
}

}  // namespace

frames::Result<std::vector<frames::MeasurementFrame>> synthesizeSParameters(
    const frames::RawReceiverFrame& rawFrame,
    std::span<const domain::MeasurementSnapshot> measurements) {
    // Aggregate-friendly adapter input is revalidated exactly once per batch.
    auto validated = frames::makeRawReceiverFrame(
        rawFrame.context, rawFrame.frequencyAxis, rawFrame.payload);
    if (!validated.hasValue()) {
        return frames::Result<std::vector<frames::MeasurementFrame>>{
            validated.error()};
    }
    std::map<domain::MeasurementType, std::vector<frames::ComplexSample>> cache;
    std::vector<frames::MeasurementFrame> output;
    output.reserve(measurements.size());
    for (const auto& measurement : measurements) {
        if (measurement.channelId != validated.value().context.channelId) {
            return frames::Result<std::vector<frames::MeasurementFrame>>{
                frames::FrameError{.code =
                    frames::FrameErrorCode::MeasurementChannelMismatch}};
        }
        auto cached = cache.find(measurement.type);
        if (cached == cache.end()) {
            std::vector<frames::ComplexSample> ratios;
            if (const auto error = calculateRatios(
                    validated.value(), measurement.type, ratios)) {
                return frames::Result<std::vector<frames::MeasurementFrame>>{
                    *error};
            }
            cached = cache.emplace(measurement.type, std::move(ratios)).first;
        }
        auto frame = frames::makeMeasurementFrame(
            validated.value().context, validated.value().frequencyAxis,
            measurement.id, measurement.type, cached->second);
        if (!frame.hasValue()) {
            return frames::Result<std::vector<frames::MeasurementFrame>>{
                frame.error()};
        }
        output.push_back(frame.value());
    }
    return frames::Result<std::vector<frames::MeasurementFrame>>{
        std::move(output)};
}

frames::Result<frames::MeasurementFrame> synthesizeSParameter(
    frames::RawReceiverFrame rawFrame,
    domain::MeasurementSnapshot measurement) {
    const std::array measurements{measurement};
    const auto batch = synthesizeSParameters(rawFrame, measurements);
    if (!batch.hasValue()) {
        return frames::Result<frames::MeasurementFrame>{batch.error()};
    }
    return frames::Result<frames::MeasurementFrame>{batch.value().front()};
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
