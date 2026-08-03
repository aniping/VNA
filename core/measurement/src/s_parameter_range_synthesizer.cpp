#include <vna/measurement/s_parameter_synthesizer.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace vna::measurement {
namespace {

struct SParameterPorts {
    std::uint32_t responsePort;
    std::uint32_t sourcePort;
};

bool finite(const frames::ComplexSample& sample) {
    return std::isfinite(sample.real) && std::isfinite(sample.imaginary);
}

std::optional<SParameterPorts> portsFor(domain::MeasurementType type) {
    switch (type) {
    case domain::MeasurementType::S11:
        return SParameterPorts{1, 1};
    case domain::MeasurementType::S21:
        return SParameterPorts{2, 1};
    case domain::MeasurementType::S12:
        return SParameterPorts{1, 2};
    case domain::MeasurementType::S22:
        return SParameterPorts{2, 2};
    }
    return std::nullopt;
}

std::optional<frames::FrameError> validateRangeShape(
    const SParameterRangeSynthesisRequest& request) {
    if (request.portCount == 0 || request.portCount > frames::kMaxPortCount) {
        return frames::FrameError{frames::FrameErrorCode::InvalidPortCount};
    }
    if (request.sourcePort == 0 || request.sourcePort > request.portCount) {
        return frames::FrameError{frames::FrameErrorCode::InvalidSourcePort};
    }
    if (request.totalPointCount > frames::kMaxSweepPoints) {
        return frames::FrameError{frames::FrameErrorCode::PointCountExceeded};
    }
    if (request.totalPointCount < 2 || request.samples.empty() ||
        request.firstPoint >= request.totalPointCount ||
        request.samples.size() >
            request.totalPointCount - request.firstPoint) {
        return frames::FrameError{frames::FrameErrorCode::SampleCountMismatch};
    }
    for (const auto& sample : request.samples) {
        if (sample.responses.size() != request.portCount) {
            return frames::FrameError{
                frames::FrameErrorCode::ResponseCountMismatch};
        }
        if (!finite(sample.reference) ||
            !std::all_of(
                sample.responses.cbegin(), sample.responses.cend(), finite)) {
            return frames::FrameError{
                frames::FrameErrorCode::NonFiniteSample};
        }
    }
    return std::nullopt;
}

frames::ComplexSample divide(
    const frames::ComplexSample& numerator,
    const frames::ComplexSample& denominator) {
    const auto squared = denominator.real * denominator.real +
                         denominator.imaginary * denominator.imaginary;
    return {
        (numerator.real * denominator.real +
         numerator.imaginary * denominator.imaginary) /
            squared,
        (numerator.imaginary * denominator.real -
         numerator.real * denominator.imaginary) /
            squared,
    };
}

frames::Result<std::vector<frames::ComplexSample>> calculate(
    vna::compat::Span<const frames::RawReceiverSample> samples,
    std::uint32_t responsePort) {
    std::vector<frames::ComplexSample> ratios;
    ratios.reserve(samples.size());
    for (const auto& sample : samples) {
        if (sample.reference.real == 0.0 &&
            sample.reference.imaginary == 0.0) {
            return frames::Result<std::vector<frames::ComplexSample>>{
                frames::FrameError{frames::FrameErrorCode::ZeroReference}};
        }
        const auto ratio = divide(
            sample.responses[responsePort - 1], sample.reference);
        if (!finite(ratio)) {
            return frames::Result<std::vector<frames::ComplexSample>>{
                frames::FrameError{frames::FrameErrorCode::NonFiniteSample}};
        }
        ratios.push_back(ratio);
    }
    return frames::Result<std::vector<frames::ComplexSample>>{
        std::move(ratios)};
}

}  // namespace

frames::Result<std::vector<MeasurementSampleRange>>
synthesizeSParameterRanges(const SParameterRangeSynthesisRequest& request) {
    if (const auto invalid = validateRangeShape(request)) {
        return frames::Result<std::vector<MeasurementSampleRange>>{
            *invalid};
    }
    std::map<domain::MeasurementType, std::vector<frames::ComplexSample>> cache;
    std::vector<MeasurementSampleRange> output;
    output.reserve(request.measurements.size());
    for (const auto& measurement : request.measurements) {
        if (measurement.id.value() == 0) {
            return frames::Result<std::vector<MeasurementSampleRange>>{
                frames::FrameError{
                    frames::FrameErrorCode::InvalidMeasurementId}};
        }
        const auto ports = portsFor(measurement.type);
        if (!ports) {
            return frames::Result<std::vector<MeasurementSampleRange>>{
                frames::FrameError{
                    frames::FrameErrorCode::UnsupportedMeasurementType}};
        }
        if (ports->sourcePort != request.sourcePort) {
            continue;
        }
        if (ports->responsePort > request.portCount) {
            return frames::Result<std::vector<MeasurementSampleRange>>{
                frames::FrameError{frames::FrameErrorCode::InvalidPortCount}};
        }
        auto cached = cache.find(measurement.type);
        if (cached == cache.end()) {
            auto ratios = calculate(request.samples, ports->responsePort);
            if (!ratios.hasValue()) {
                return frames::Result<std::vector<MeasurementSampleRange>>{
                    ratios.error()};
            }
            cached = cache.emplace(
                measurement.type, ratios.value()).first;
        }
        output.push_back({
            request.firstPoint,
            measurement.id,
            measurement.type,
            cached->second,
        });
    }
    return frames::Result<std::vector<MeasurementSampleRange>>{
        std::move(output)};
}

}  // namespace vna::measurement
