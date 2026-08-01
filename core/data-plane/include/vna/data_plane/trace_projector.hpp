#pragma once

#include <variant>
#include <vector>

#include <vna/display_model/display_workspace.hpp>
#include <vna/frames/frames.hpp>

namespace vna::data_plane {

enum class ProjectedTraceUnit {
    Decibel,
    Degree,
    Unitless,
};

struct ScalarTraceSamples {
    ProjectedTraceUnit unit;
    std::vector<double> values;
};

struct ComplexTraceSamples {
    ProjectedTraceUnit unit;
    std::vector<frames::ComplexSample> values;
};

// The variant keeps scalar and complex display data distinct. LogMagnitude is
// 20*log10(|Sij|) in dB and rejects zero magnitude rather than returning an
// infinity. Phase is pointwise atan2 in degrees, wrapped to [-180, 180), maps
// complex zero to 0 degrees, and deliberately does not unwrap across points.
// Smith receives complex Sij unchanged; coordinate mapping remains a UI task.
using ProjectedTraceSamples =
    std::variant<ScalarTraceSamples, ComplexTraceSamples>;

[[nodiscard]] frames::Result<ProjectedTraceSamples> projectTraceSamples(
    const frames::MeasurementFrame& source,
    display_model::TraceFormat format);

}  // namespace vna::data_plane
