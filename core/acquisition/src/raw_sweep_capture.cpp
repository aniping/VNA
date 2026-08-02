#include <vna/acquisition/raw_sweep_capture.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace vna::acquisition {
namespace {

frames::FrameError error(frames::FrameErrorCode code) {
    return frames::FrameError{code};
}

std::optional<frames::FrameError> validateRequest(
    const RawSweepCaptureRequest& request) {
    const auto& plan = request.plan;
    const auto& axis = plan.frequencyAxis;
    if (request.sweepId.value() == 0 || request.sequenceNumber == 0 ||
        request.maximumPointsPerChunk == 0 || axis.id.value() == 0 ||
        axis.startFrequencyHz >= axis.stopFrequencyHz || axis.points < 2 ||
        axis.points > frames::kMaxSweepPoints || plan.portCount == 0 ||
        plan.portCount > frames::kMaxPortCount || plan.sourcePorts.empty() ||
        plan.sourcePorts.size() > plan.portCount || plan.ifBandwidthHz == 0 ||
        !std::isfinite(plan.powerDbm)) {
        return error(frames::FrameErrorCode::InvalidAcquisitionSettings);
    }
    std::vector<bool> seen(plan.portCount + 1, false);
    for (const auto port : plan.sourcePorts) {
        if (port == 0 || port > plan.portCount || seen[port]) {
            return error(frames::FrameErrorCode::InvalidSourcePort);
        }
        seen[port] = true;
    }
    return std::nullopt;
}

bool finite(const frames::ComplexSample& sample) {
    return std::isfinite(sample.real) && std::isfinite(sample.imaginary);
}

std::optional<frames::FrameError> validateRange(
    const RawSweepPointRange& range,
    const RawSweepChunkRequest& expected,
    std::uint32_t portCount) {
    if (range.sourcePort != expected.sourcePort) {
        return error(frames::FrameErrorCode::InvalidSourcePort);
    }
    if (range.firstPoint != expected.firstPoint ||
        range.samples.size() != expected.pointCount) {
        return error(frames::FrameErrorCode::SampleCountMismatch);
    }
    for (const auto& sample : range.samples) {
        if (sample.responses.size() != portCount) {
            return error(frames::FrameErrorCode::ResponseCountMismatch);
        }
        const auto responsesFinite = std::all_of(
            sample.responses.cbegin(), sample.responses.cend(), finite);
        if (!finite(sample.reference) || !responsesFinite) {
            return error(frames::FrameErrorCode::NonFiniteSample);
        }
    }
    return std::nullopt;
}

RawSweepChunkRequest makeChunkRequest(
    const RawSweepCaptureRequest& capture,
    std::uint32_t sourcePort,
    std::uint32_t firstPoint) {
    const auto remaining = capture.plan.frequencyAxis.points - firstPoint;
    return {
        capture.sweepId,
        capture.sequenceNumber,
        sourcePort,
        firstPoint,
        std::min(remaining, capture.maximumPointsPerChunk),
    };
}

}  // namespace

RawSweepCaptureResult captureRawSweep(
    const RawSweepCaptureRequest& request,
    const RawSweepChunkProducer& producer,
    const RawSweepChunkObserver& observer,
    std::stop_token token) {
    if (const auto invalid = validateRequest(request)) {
        return *invalid;
    }
    if (!producer) {
        return error(frames::FrameErrorCode::InvalidAcquisitionSettings);
    }
    frames::RawReceiverPayload payload{
        .portCount = request.plan.portCount,
        .sourceStates = {},
    };
    payload.sourceStates.reserve(request.plan.sourcePorts.size());
    for (const auto sourcePort : request.plan.sourcePorts) {
        frames::RawSourceState source{
            .sourcePort = sourcePort,
            .samples = {},
        };
        source.samples.reserve(request.plan.frequencyAxis.points);
        for (std::uint32_t firstPoint = 0;
             firstPoint < request.plan.frequencyAxis.points;) {
            if (token.stop_requested()) {
                return RawSweepCaptureCanceled{};
            }
            const auto chunkRequest =
                makeChunkRequest(request, sourcePort, firstPoint);
            auto produced = producer(request.plan, chunkRequest, token);
            if (token.stop_requested() ||
                std::holds_alternative<RawSweepCaptureCanceled>(produced)) {
                return RawSweepCaptureCanceled{};
            }
            if (const auto* failure =
                    std::get_if<frames::FrameError>(&produced)) {
                return *failure;
            }
            auto chunk =
                std::move(std::get<RawSweepPointRange>(produced));
            if (const auto invalid = validateRange(
                    chunk, chunkRequest, request.plan.portCount)) {
                return *invalid;
            }
            source.samples.insert(
                source.samples.end(),
                chunk.samples.cbegin(), chunk.samples.cend());
            if (observer) {
                observer(chunk);
            }
            firstPoint += chunkRequest.pointCount;
        }
        payload.sourceStates.push_back(std::move(source));
    }
    return payload;
}

}  // namespace vna::acquisition
