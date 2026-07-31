#pragma once

#include <vna/frames/frames.hpp>

namespace vna::measurement {

// Consumes one complete raw receiver frame and produces S11 = b1 / a1.
// Display formatting is intentionally excluded so the same complex result can
// later feed Log Magnitude, Phase, Smith, markers, and file export.
[[nodiscard]] frames::Result<frames::MeasurementFrame> synthesizeS11(
    frames::RawReceiverFrame rawFrame,
    domain::MeasurementSnapshot measurement);

}  // namespace vna::measurement
