#include "frequency_axis_materialization_internal.hpp"

#include <cmath>
#include <cstdint>

namespace vna::application::internal {

std::optional<std::vector<double>> materializeFrequencies(
    const frames::FrequencyAxis& axis) {
    std::vector<double> values;
    values.reserve(axis.points);
    const auto start = static_cast<long double>(axis.startFrequencyHz);
    const auto stop = static_cast<long double>(axis.stopFrequencyHz);
    const auto intervals = static_cast<long double>(axis.points - 1);
    for (std::uint32_t index = 0; index < axis.points; ++index) {
        // Force both endpoints, then interpolate in long double so rounding is
        // deferred until the final display value conversion.
        const auto exact = index == 0
                               ? start
                               : (index + 1 == axis.points
                                      ? stop
                                      : start + (stop - start) * index /
                                                    intervals);
        const auto frequency = static_cast<double>(exact);
        if (!std::isfinite(frequency) ||
            (!values.empty() && frequency <= values.back())) {
            return std::nullopt;
        }
        values.push_back(frequency);
    }
    return values;
}

}  // namespace vna::application::internal
