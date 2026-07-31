#pragma once

#include <vector>

#include <vna/frames/frames.hpp>

namespace vna::data_plane {

// Projects complex network samples to display-domain dB values. Pixel mapping
// and Trace scale remain presentation concerns; callers receive the complete
// sample vector at the original frequency-axis resolution.
[[nodiscard]] frames::Result<std::vector<double>> projectLogMagnitude(
    const frames::MeasurementFrame& source);

}  // namespace vna::data_plane
