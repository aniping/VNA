#pragma once

#include <vna/frames/frames.hpp>

namespace vna::measurement {

// Selects Sij as response receiver i divided by the reference captured while
// source port j was active. Measurement names only choose matrix coordinates;
// validation and complex division stay on one processing path.
[[nodiscard]] frames::Result<frames::MeasurementFrame> synthesizeSParameter(
    frames::RawReceiverFrame rawFrame,
    domain::MeasurementSnapshot measurement);

}  // namespace vna::measurement
