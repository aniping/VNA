#include <vna/measurement/s11_synthesizer.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace vna::measurement {
namespace {

std::optional<frames::FrameError> validateMeasurementChannels(
    domain::ChannelId channelId,
    vna::compat::Span<const domain::MeasurementSnapshot> measurements) {
    const auto wrongChannel = std::find_if(
        measurements.begin(), measurements.end(),
        [channelId](const auto& measurement) {
            return measurement.channelId != channelId;
        });
    if (wrongChannel != measurements.end()) {
        return frames::FrameError{
            frames::FrameErrorCode::MeasurementChannelMismatch};
    }
    return std::nullopt;
}

frames::Result<std::vector<MeasurementSampleRange>> synthesizeAllRanges(
    const frames::RawReceiverFrame& rawFrame,
    vna::compat::Span<const domain::MeasurementSnapshot> measurements) {
    std::vector<MeasurementSampleRange> ranges;
    ranges.reserve(measurements.size());
    for (const auto& source : rawFrame.payload.sourceStates) {
        const auto result = synthesizeSParameterRanges({
            source.sourcePort,
            0,
            rawFrame.frequencyAxis.points,
            rawFrame.payload.portCount,
            source.samples,
            measurements,
        });
        if (!result.hasValue()) {
            return frames::Result<std::vector<MeasurementSampleRange>>{
                result.error()};
        }
        ranges.insert(
            ranges.end(), result.value().cbegin(), result.value().cend());
    }
    return frames::Result<std::vector<MeasurementSampleRange>>{
        std::move(ranges)};
}

const MeasurementSampleRange* findRange(
    const std::vector<MeasurementSampleRange>& ranges,
    const domain::MeasurementSnapshot& measurement) {
    const auto found = std::find_if(
        ranges.cbegin(), ranges.cend(), [&measurement](const auto& range) {
            return range.measurementId == measurement.id &&
                   range.type == measurement.type;
        });
    return found == ranges.cend() ? nullptr : &*found;
}

}  // namespace

frames::Result<std::vector<frames::MeasurementFrame>> synthesizeSParameters(
    const frames::RawReceiverFrame& rawFrame,
    vna::compat::Span<const domain::MeasurementSnapshot> measurements) {
    const auto validated = frames::makeRawReceiverFrame(
        rawFrame.context, rawFrame.frequencyAxis, rawFrame.payload);
    if (!validated.hasValue()) {
        return frames::Result<std::vector<frames::MeasurementFrame>>{
            validated.error()};
    }
    if (const auto invalid = validateMeasurementChannels(
            validated.value().context.channelId, measurements)) {
        return frames::Result<std::vector<frames::MeasurementFrame>>{*invalid};
    }
    const auto ranges = synthesizeAllRanges(validated.value(), measurements);
    if (!ranges.hasValue()) {
        return frames::Result<std::vector<frames::MeasurementFrame>>{
            ranges.error()};
    }
    std::vector<frames::MeasurementFrame> output;
    output.reserve(measurements.size());
    for (const auto& measurement : measurements) {
        const auto* range = findRange(ranges.value(), measurement);
        if (range == nullptr) {
            return frames::Result<std::vector<frames::MeasurementFrame>>{
                frames::FrameError{frames::FrameErrorCode::InvalidSourcePort}};
        }
        auto frame = frames::makeMeasurementFrame(
            validated.value().context, validated.value().frequencyAxis,
            measurement.id, measurement.type, range->samples);
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
            frames::FrameErrorCode::UnsupportedMeasurementType}};
    }
    return synthesizeSParameter(std::move(rawFrame), measurement);
}

}  // namespace vna::measurement
