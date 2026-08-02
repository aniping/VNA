#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vna/frames/frames.hpp>

namespace vna::measurement {

struct SParameterRangeSynthesisRequest {
    std::uint32_t sourcePort;
    std::uint32_t firstPoint;
    std::uint32_t totalPointCount;
    std::uint32_t portCount;
    std::span<const frames::RawReceiverSample> samples;
    std::span<const domain::MeasurementSnapshot> measurements;
};

struct MeasurementSampleRange {
    std::uint32_t firstPoint;
    domain::MeasurementId measurementId;
    domain::MeasurementType type;
    std::vector<frames::ComplexSample> samples;
};

// Synthesizes only measurements driven by this source state. The returned
// ranges preserve request order and never masquerade as complete frames.
[[nodiscard]] frames::Result<std::vector<MeasurementSampleRange>>
synthesizeSParameterRanges(const SParameterRangeSynthesisRequest& request);

// Selects Sij as response receiver i divided by the reference captured while
// source port j was active. Measurement names only choose matrix coordinates;
// validation and complex division stay on one processing path.
[[nodiscard]] frames::Result<frames::MeasurementFrame> synthesizeSParameter(
    frames::RawReceiverFrame rawFrame,
    domain::MeasurementSnapshot measurement);

// Validates one receiver frame, then reuses each requested Sij calculation.
// Output order follows the request; any invalid measurement fails atomically.
[[nodiscard]] frames::Result<std::vector<frames::MeasurementFrame>>
synthesizeSParameters(
    const frames::RawReceiverFrame& rawFrame,
    std::span<const domain::MeasurementSnapshot> measurements);

}  // namespace vna::measurement
