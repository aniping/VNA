#pragma once

#include <cstdint>
#include <functional>
#include <stop_token>
#include <variant>
#include <vector>

#include <vna/acquisition/continuous_acquisition.hpp>

namespace vna::acquisition {

struct RawSweepCaptureRequest {
    ContinuousAcquisitionPlan plan;
    SweepId sweepId;
    std::uint64_t sequenceNumber;
    std::uint32_t maximumPointsPerChunk;
};

// A range is temporary acquisition progress, never a partial RawReceiverPayload.
// firstPoint is zero-based in the immutable plan's frequency-axis order.
struct RawSweepPointRange {
    std::uint32_t sourcePort;
    std::uint32_t firstPoint;
    std::vector<frames::RawReceiverSample> samples;
    friend bool operator==(
        const RawSweepPointRange&,
        const RawSweepPointRange&) = default;
};

struct RawSweepChunkRequest {
    SweepId sweepId;
    std::uint64_t sequenceNumber;
    std::uint32_t sourcePort;
    std::uint32_t firstPoint;
    std::uint32_t pointCount;
    friend bool operator==(
        const RawSweepChunkRequest&,
        const RawSweepChunkRequest&) = default;
};

struct RawSweepCaptureCanceled {};

using RawSweepChunkResult = std::variant<
    RawSweepPointRange,
    frames::FrameError,
    RawSweepCaptureCanceled>;
using RawSweepCaptureResult = std::variant<
    frames::RawReceiverPayload,
    frames::FrameError,
    RawSweepCaptureCanceled>;

// The producer is called serially by the sole capture owner. It must return
// after cancellation; the capture will discard every accumulated partial range.
using RawSweepChunkProducer = std::function<RawSweepChunkResult(
    const ContinuousAcquisitionPlan&,
    RawSweepChunkRequest,
    std::stop_token)>;
using RawSweepChunkObserver =
    std::function<void(const RawSweepPointRange&)>;

// Drives source states and point ranges in plan order. The observer is a
// synchronous progress seam and must return promptly; only the final return
// value can contain a complete RawReceiverPayload.
[[nodiscard]] RawSweepCaptureResult captureRawSweep(
    const RawSweepCaptureRequest& request,
    const RawSweepChunkProducer& producer,
    const RawSweepChunkObserver& observer = {},
    std::stop_token token = {});

}  // namespace vna::acquisition
