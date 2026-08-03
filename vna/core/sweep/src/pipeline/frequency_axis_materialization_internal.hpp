#pragma once

#include <optional>
#include <vector>

#include <vna/frames/raw_receiver.hpp>

namespace vna::application::internal {

// Display frames carry an explicit axis so consumers never need to duplicate
// interpolation or make assumptions about endpoint rounding.
[[nodiscard]] std::optional<std::vector<double>> materializeFrequencies(
    const frames::FrequencyAxis& axis);

}  // namespace vna::application::internal
