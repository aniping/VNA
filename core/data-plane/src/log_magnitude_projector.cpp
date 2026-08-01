#include <vna/data_plane/log_magnitude_projector.hpp>

#include <variant>

#include <vna/data_plane/trace_projector.hpp>

namespace vna::data_plane {

frames::Result<std::vector<double>> projectLogMagnitude(
    const frames::MeasurementFrame& source) {
    const auto projected = projectTraceSamples(
        source,
        display_model::TraceFormat::LogMagnitude);
    if (!projected.hasValue()) {
        return frames::Result<std::vector<double>>{projected.error()};
    }
    const auto& scalar = std::get<ScalarTraceSamples>(projected.value());
    return frames::Result<std::vector<double>>{scalar.values};
}

}  // namespace vna::data_plane
