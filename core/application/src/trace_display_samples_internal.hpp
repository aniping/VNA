#pragma once

#include <optional>
#include <type_traits>

#include <vna/application/trace_display_frame.hpp>
#include <vna/data_plane/trace_projector.hpp>

namespace vna::application::internal {

// Complete and partial sweep paths share this fail-closed mapping so a new
// projection unit cannot silently acquire a different wire meaning in either.
inline std::optional<TraceDisplaySamples> toTraceDisplaySamples(
    const data_plane::ProjectedTraceSamples& projected) {
    return std::visit(
        [](const auto& samples) -> std::optional<TraceDisplaySamples> {
            using Samples = std::decay_t<decltype(samples)>;
            if constexpr (std::is_same_v<
                              Samples, data_plane::ScalarTraceSamples>) {
                if (samples.unit == data_plane::ProjectedTraceUnit::Decibel) {
                    return CartesianTraceDisplaySamples{
                        TraceDisplayUnit::Decibel, samples.values};
                }
                if (samples.unit == data_plane::ProjectedTraceUnit::Degree) {
                    return CartesianTraceDisplaySamples{
                        TraceDisplayUnit::Degree, samples.values};
                }
            } else if (samples.unit ==
                       data_plane::ProjectedTraceUnit::Unitless) {
                return ComplexTraceDisplaySamples{
                    TraceDisplayUnit::Unitless, samples.values};
            }
            return std::nullopt;
        },
        projected);
}

}  // namespace vna::application::internal
