#include "display_frame_json_codec.hpp"

#include <nlohmann/json.hpp>

namespace vna::web_api::detail {

std::string encodeDisplayFrame(
    const application::TraceDisplayFrame& frame) {
    // The application repository admits only LogMagnitude/dB frames. Keeping
    // those fixed wire labels here prevents the transport from reinterpreting
    // measurement samples or inventing another presentation model.
    return nlohmann::json{
        {"frameId", frame.frameId.value()},
        {"traceId", frame.traceId.value()},
        {"stateRevision", frame.stateRevision},
        {"sequenceNumber", frame.sequenceNumber},
        {"format", "logMagnitude"},
        {"valueUnit", "dB"},
        {"frequenciesHz", frame.frequenciesHz},
        {"values", frame.values},
    }.dump();
}

}  // namespace vna::web_api::detail
