#pragma once

#include <optional>

#include <vna/acquisition/continuous_acquisition.hpp>
#include <vna/application/trace_display_frame_set.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application::internal {

// One raw sweep is expanded into a complete immutable publication candidate;
// returning null rejects the whole sweep rather than exposing partial traces.
[[nodiscard]] std::optional<TraceDisplayFrameSet> buildTraceDisplayFrameSet(
    const acquisition::RawFrame& raw,
    const TracePublicationPlan& plan);

}  // namespace vna::application::internal
