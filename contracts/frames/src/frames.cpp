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
    return isFinite(sample.reference) &&
           std::all_of(
               sample.responses.cbegin(), sample.responses.cend(),
               [](const ComplexSample& response) { return isFinite(response); });
}

std::optional<FrameError> validatePayload(
    const RawReceiverPayload& payload,
    std::uint32_t points) {
    if (payload.portCount == 0 || payload.portCount > kMaxPortCount) {
        return FrameError{.code = FrameErrorCode::InvalidPortCount};
    }
    if (payload.sourceStates.empty() ||
        payload.sourceStates.size() > payload.portCount) {
        return FrameError{.code = FrameErrorCode::InvalidSourcePort};
    }
    std::vector<bool> seen(payload.portCount + 1, false);
    for (const auto& state : payload.sourceStates) {
        if (state.sourcePort == 0 || state.sourcePort > payload.portCount) {
            return FrameError{.code = FrameErrorCode::InvalidSourcePort};
        }
        if (seen[state.sourcePort]) {
            return FrameError{.code = FrameErrorCode::DuplicateSourcePort};
        }
        seen[state.sourcePort] = true;
        if (state.samples.size() != points) {
            return FrameError{.code = FrameErrorCode::SampleCountMismatch};
        }
        for (const auto& sample : state.samples) {
            if (sample.responses.size() != payload.portCount) {
                return FrameError{.code = FrameErrorCode::ResponseCountMismatch};
            }
            if (!isFinite(sample)) {
                return FrameError{.code = FrameErrorCode::NonFiniteSample};
            }
        }
    }
    return std::nullopt;
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
    if (const auto error = validatePayload(payload, frequencyAxis.points)) {
        return Result<RawReceiverFrame>{*error};
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
