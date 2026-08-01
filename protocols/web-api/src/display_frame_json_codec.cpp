#include "display_frame_json_codec.hpp"

#include <nlohmann/json.hpp>

namespace vna::web_api::detail {

std::string encodeDisplayFrame(
    const application::TraceDisplayFrame& frame) {
    // This transport slice still exposes the established LogMagnitude wire
    // shape. The variant lookup is a mechanical adaptation; Phase and Smith
    // protocol forms remain outside this contract change.
    const auto& samples =
        std::get<application::CartesianTraceDisplaySamples>(frame.samples);
    return nlohmann::json{
        {"frameId", frame.frameId.value()},
        {"traceId", frame.traceId.value()},
        {"stateRevision", frame.stateRevision},
        {"sequenceNumber", frame.sequenceNumber},
        {"format", "logMagnitude"},
        {"valueUnit", "dB"},
        {"frequenciesHz", frame.frequenciesHz},
        {"values", samples.values},
    }.dump();
}

}  // namespace vna::web_api::detail
