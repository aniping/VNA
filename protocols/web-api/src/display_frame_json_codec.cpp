#include "display_frame_json_codec.hpp"
#include "json_codec.hpp"

#include <nlohmann/json.hpp>

#include <utility>
#include <variant>

namespace {

using Json = nlohmann::json;

Json frameCommon(const vna::application::TraceDisplayFrame& frame) {
    return {
        {"frameId", frame.frameId.value()},
        {"traceId", frame.traceId.value()},
        {"measurementId", frame.measurementId.value()},
        {"measurementType",
         vna::web_api::detail::measurementTypeName(frame.measurementType)},
        {"generation", frame.generation},
        {"stateRevision", frame.stateRevision},
        {"sequenceNumber", frame.sequenceNumber},
        {"frequenciesHz", frame.frequenciesHz},
    };
}

const char* cartesianUnit(vna::display_model::TraceFormat format) {
    switch (format) {
        case vna::display_model::TraceFormat::LogMagnitude:
            return "dB";
        case vna::display_model::TraceFormat::Phase:
            return "degree";
        case vna::display_model::TraceFormat::Smith:
            break;
    }
    return "unknown";
}

Json frameToJson(const vna::application::TraceDisplayFrame& frame) {
    auto body = frameCommon(frame);
    if (frame.format == vna::display_model::TraceFormat::Smith) {
        body["format"] =
            vna::web_api::detail::traceFormatName(frame.format);
        body["valueUnit"] = "U";
        body["values"] = Json::array();
        const auto& samples = std::get<
            vna::application::ComplexTraceDisplaySamples>(frame.samples);
        for (const auto& sample : samples.values) {
            body["values"].push_back({sample.real, sample.imaginary});
        }
        return body;
    }
    const auto& samples = std::get<
        vna::application::CartesianTraceDisplaySamples>(frame.samples);
    body["format"] = vna::web_api::detail::traceFormatName(frame.format);
    body["valueUnit"] = cartesianUnit(frame.format);
    body["values"] = samples.values;
    return body;
}

}  // namespace

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

std::string encodeDisplayFrameSet(
    const application::TraceDisplayFrameSet& frameSet) {
    Json frames = Json::array();
    for (const auto& frame : frameSet.frames) {
        frames.push_back(frameToJson(frame));
    }
    return Json{
        {"generation", frameSet.generation},
        {"sequenceNumber", frameSet.sequenceNumber},
        {"frames", std::move(frames)},
    }.dump();
}

}  // namespace vna::web_api::detail
