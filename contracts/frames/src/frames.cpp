#include <vna/frames/frames.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace vna::frames {
namespace {

bool isValidContext(const FrameContext& context) {
    return context.frameId.value() != 0 &&
           context.sweepId.value() != 0 &&
           context.channelId.value() != 0 &&
           context.sequenceNumber != 0;
}

std::optional<FrameError> validateAxis(const FrequencyAxis& axis) {
    if (axis.points > kMaxSweepPoints) {
        return FrameError{.code = FrameErrorCode::PointCountExceeded};
    }
    if (axis.id.value() == 0 ||
        axis.startFrequencyHz >= axis.stopFrequencyHz ||
        axis.points < 2) {
        return FrameError{.code = FrameErrorCode::InvalidFrequencyAxis};
    }
    return std::nullopt;
}

bool isFinite(const ComplexSample& sample) {
    return std::isfinite(sample.real) && std::isfinite(sample.imaginary);
}

bool isFinite(const RawReceiverSample& sample) {
    return isFinite(sample.a1) && isFinite(sample.b1);
}

}  // namespace

Result<RawReceiverFrame> makeRawReceiverFrame(
    FrameContext context,
    FrequencyAxis frequencyAxis,
    RawReceiverPayload payload) {
    if (!isValidContext(context)) {
        return Result<RawReceiverFrame>{
            FrameError{.code = FrameErrorCode::InvalidFrameContext}};
    }
    if (const auto error = validateAxis(frequencyAxis)) {
        return Result<RawReceiverFrame>{*error};
    }
    if (payload.samples.size() != frequencyAxis.points) {
        return Result<RawReceiverFrame>{
            FrameError{.code = FrameErrorCode::SampleCountMismatch}};
    }
    if (!std::all_of(
            payload.samples.cbegin(), payload.samples.cend(),
            [](const RawReceiverSample& sample) { return isFinite(sample); })) {
        return Result<RawReceiverFrame>{
            FrameError{.code = FrameErrorCode::NonFiniteSample}};
    }
    return Result<RawReceiverFrame>{RawReceiverFrame{
        .context = context,
        .frequencyAxis = frequencyAxis,
        .payload = std::move(payload),
    }};
}

Result<MeasurementFrame> makeMeasurementFrame(
    FrameContext context,
    FrequencyAxis frequencyAxis,
    domain::MeasurementId measurementId,
    domain::MeasurementType type,
    std::vector<ComplexSample> samples) {
    if (!isValidContext(context)) {
        return Result<MeasurementFrame>{
            FrameError{.code = FrameErrorCode::InvalidFrameContext}};
    }
    if (const auto error = validateAxis(frequencyAxis)) {
        return Result<MeasurementFrame>{*error};
    }
    if (measurementId.value() == 0) {
        return Result<MeasurementFrame>{
            FrameError{.code = FrameErrorCode::InvalidMeasurementId}};
    }
    if (type != domain::MeasurementType::S11) {
        return Result<MeasurementFrame>{
            FrameError{.code = FrameErrorCode::UnsupportedMeasurementType}};
    }
    if (samples.size() != frequencyAxis.points) {
        return Result<MeasurementFrame>{
            FrameError{.code = FrameErrorCode::SampleCountMismatch}};
    }
    if (!std::all_of(
            samples.cbegin(), samples.cend(),
            [](const ComplexSample& sample) { return isFinite(sample); })) {
        return Result<MeasurementFrame>{
            FrameError{.code = FrameErrorCode::NonFiniteSample}};
    }
    return Result<MeasurementFrame>{MeasurementFrame{
        .context = context,
        .frequencyAxis = frequencyAxis,
        .measurementId = measurementId,
        .type = type,
        .samples = std::move(samples),
    }};
}

}  // namespace vna::frames
