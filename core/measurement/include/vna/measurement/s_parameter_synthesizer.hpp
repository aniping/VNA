#pragma once

#include <span>
#include <vector>

#include <vna/frames/frames.hpp>

namespace vna::measurement {

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
