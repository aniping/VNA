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

void appendSamples(
    Json& body,
    vna::display_model::TraceFormat format,
    const vna::application::TraceDisplaySamples& samples) {
    body["format"] = vna::web_api::detail::traceFormatName(format);
    if (format == vna::display_model::TraceFormat::Smith) {
        body["valueUnit"] = "U";
        body["values"] = Json::array();
        const auto& complex = std::get<
            vna::application::ComplexTraceDisplaySamples>(samples);
        for (const auto& sample : complex.values) {
            body["values"].push_back({sample.real, sample.imaginary});
        }
        return;
    }
    const auto& cartesian = std::get<
        vna::application::CartesianTraceDisplaySamples>(samples);
    body["valueUnit"] = cartesianUnit(format);
    body["values"] = cartesian.values;
}

Json frameToJson(const vna::application::TraceDisplayFrame& frame) {
    auto body = frameCommon(frame);
    appendSamples(body, frame.format, frame.samples);
    return body;
}

Json previewTraceToJson(const vna::application::SweepTracePreview& trace) {
    Json body{
        {"traceId", trace.traceId.value()},
        {"measurementId", trace.measurementId.value()},
        {"measurementType",
         vna::web_api::detail::measurementTypeName(trace.measurementType)},
        {"frequenciesHz", trace.frequenciesHz},
    };
    appendSamples(body, trace.format, trace.samples);
    return body;
}

Json previewEventToJson(const vna::application::SweepPreviewAvailable& event) {
    const auto& preview = *event.preview;
    Json traces = Json::array();
    for (const auto& trace : preview.traces) {
        traces.push_back(previewTraceToJson(trace));
    }
    return {
        {"type", "available"},
        {"eventCursor", event.cursor.value},
        {"generation", preview.identity.generation},
        {"sweepId", preview.identity.sweepId.value()},
        {"channelId", preview.channelId.value()},
        {"stateRevision", preview.stateRevision},
        {"sequenceNumber", preview.sequenceNumber},
        {"totalPointCount", preview.totalPointCount},
        {"traces", std::move(traces)},
    };
}

Json previewEventToJson(const vna::application::SweepPreviewInvalidated& event) {
    return {
        {"type", "invalidated"},
        {"eventCursor", event.cursor.value},
        {"generation", event.identity.generation},
        {"sweepId", event.identity.sweepId.value()},
    };
}

Json previewEventToJson(
    const vna::application::SweepPreviewGenerationAdvanced& event) {
    return {
        {"type", "generationAdvanced"},
        {"eventCursor", event.cursor.value},
        {"generation", event.generation},
    };
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

std::string encodeSweepPreviewEvent(
    const application::SweepPreviewEvent& event) {
    return std::visit(
        [](const auto& value) { return previewEventToJson(value).dump(); },
        event);
}

}  // namespace vna::web_api::detail
