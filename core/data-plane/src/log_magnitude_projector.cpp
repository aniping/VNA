#include <vna/data_plane/log_magnitude_projector.hpp>

#include <cmath>
#include <utility>
#include <vector>

namespace vna::data_plane {

frames::Result<std::vector<double>> projectLogMagnitude(
    const frames::MeasurementFrame& source) {
    // MeasurementFrame remains an aggregate at adapter boundaries. Reusing its
    // public factory here prevents malformed or non-S11 data from silently
    // entering display calculations when a caller bypasses the factory.
    const auto validated = frames::makeMeasurementFrame(
        source.context,
        source.frequencyAxis,
        source.measurementId,
        source.type,
        source.samples);
    if (!validated.hasValue()) {
        return frames::Result<std::vector<double>>{validated.error()};
    }

    std::vector<double> values;
    values.reserve(validated.value().samples.size());
    for (const auto& sample : validated.value().samples) {
        const auto magnitude = std::hypot(sample.real, sample.imaginary);
        const auto projected = 20.0 * std::log10(magnitude);
        // Zero magnitude maps to negative infinity. Treat that as a complete
        // projection failure so no consumer can mistake a partial dB vector
        // for a publishable Trace frame.
        if (!std::isfinite(projected)) {
            return frames::Result<std::vector<double>>{frames::FrameError{
                .code = frames::FrameErrorCode::NonFiniteTraceValue}};
        }
        values.push_back(projected);
    }
    return frames::Result<std::vector<double>>{std::move(values)};
}

}  // namespace vna::data_plane
