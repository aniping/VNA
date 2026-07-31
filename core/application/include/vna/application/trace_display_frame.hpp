#pragma once

#include <cstdint>
#include <vector>

#include <vna/display_model/display_workspace.hpp>
#include <vna/frames/frames.hpp>

namespace vna::application {

// Once published, this DTO is the immutable handoff consumed by display
// queries. It owns derived values only; raw receiver and complex measurement
// samples stay below the application boundary.
struct TraceDisplayFrame {
    frames::FrameId frameId;
    display_model::TraceId traceId;
    std::uint64_t stateRevision;
    std::uint64_t sequenceNumber;
    display_model::TraceFormat format;
    display_model::ScaleUnit valueUnit;
    std::vector<double> frequenciesHz;
    std::vector<double> values;
};

}  // namespace vna::application
