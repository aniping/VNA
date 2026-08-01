#include <vna/data_plane/trace_projector.hpp>

#include <cmath>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace vna::data_plane {
namespace {

std::optional<frames::FrameError> projectLogMagnitudeValues(
    const frames::MeasurementFrame& source,
    std::vector<double>& values) {
    values.reserve(source.samples.size());
    for (const auto& sample : source.samples) {
        const auto projected =
            20.0 * std::log10(std::hypot(sample.real, sample.imaginary));
        // Zero magnitude maps to negative infinity. Reject the complete vector
        // so consumers never mistake partial values for a publishable Trace.
        if (!std::isfinite(projected)) {
            return frames::FrameError{
                .code = frames::FrameErrorCode::NonFiniteTraceValue};
        }
        values.push_back(projected);
    }
    return std::nullopt;
}

std::vector<double> projectPhaseValues(
    const frames::MeasurementFrame& source) {
    std::vector<double> values;
    values.reserve(source.samples.size());
    for (const auto& sample : source.samples) {
        if (sample.real == 0.0 && sample.imaginary == 0.0) {
            values.push_back(0.0);
            continue;
        }
        auto degrees = std::atan2(sample.imaginary, sample.real) * 180.0 /
                       std::numbers::pi_v<double>;
        if (degrees >= 180.0) {
            degrees -= 360.0;
        }
        values.push_back(degrees == 0.0 ? 0.0 : degrees);
    }
    return values;
}

}  // namespace

frames::Result<ProjectedTraceSamples> projectTraceSamples(
    const frames::MeasurementFrame& source,
    display_model::TraceFormat format) {
    const auto validated = frames::makeMeasurementFrame(
        source.context, source.frequencyAxis, source.measurementId,
        source.type, source.samples);
    if (!validated.hasValue()) {
        return frames::Result<ProjectedTraceSamples>{validated.error()};
    }
    switch (format) {
    case display_model::TraceFormat::LogMagnitude: {
        std::vector<double> values;
        if (const auto error =
                projectLogMagnitudeValues(validated.value(), values)) {
            return frames::Result<ProjectedTraceSamples>{*error};
        }
        return frames::Result<ProjectedTraceSamples>{ScalarTraceSamples{
            .unit = ProjectedTraceUnit::Decibel,
            .values = std::move(values)}};
    }
    case display_model::TraceFormat::Phase:
        return frames::Result<ProjectedTraceSamples>{ScalarTraceSamples{
            .unit = ProjectedTraceUnit::Degree,
            .values = projectPhaseValues(validated.value())}};
    case display_model::TraceFormat::Smith:
        return frames::Result<ProjectedTraceSamples>{ComplexTraceSamples{
            .unit = ProjectedTraceUnit::Unitless,
            .values = validated.value().samples}};
    }
    return frames::Result<ProjectedTraceSamples>{frames::FrameError{
        .code = frames::FrameErrorCode::UnsupportedTraceFormat}};
}

}  // namespace vna::data_plane
