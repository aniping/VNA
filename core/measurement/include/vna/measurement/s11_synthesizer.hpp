#pragma once

#include <vna/measurement/s_parameter_synthesizer.hpp>

namespace vna::measurement {

// Compatibility entry for the existing single-sweep application. New
// measurement code should use synthesizeSParameter so S11 and S21 cannot grow
// separate validation or arithmetic paths.
[[nodiscard]] frames::Result<frames::MeasurementFrame> synthesizeS11(
    frames::RawReceiverFrame rawFrame,
    domain::MeasurementSnapshot measurement);

}  // namespace vna::measurement
