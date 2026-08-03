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

const char* userPhaseName(vna::application::SweepUserPhase phase) {
    switch (phase) {
        case vna::application::SweepUserPhase::Hold: return "hold";
        case vna::application::SweepUserPhase::Preparing: return "preparing";
        case vna::application::SweepUserPhase::Sweeping: return "sweeping";
        case vna::application::SweepUserPhase::Calculation:
            return "calculation";
        case vna::application::SweepUserPhase::Failed: return "failed";
    }
    return "unknown";
}

Json sweepStatusToJson(
    const vna::application::SweepPreviewStreamStatus& status) {
    Json sweepId = nullptr;
    if (status.runtime.sweepId.has_value()) {
        sweepId = status.runtime.sweepId->value();
    }
    Json active = nullptr;
    if (status.activePreviewIdentity.has_value()) {
        active = {{"generation", status.activePreviewIdentity->generation},
                  {"sweepId", status.activePreviewIdentity->sweepId.value()}};
    }
    return {
        {"generation", status.runtime.generation},
        {"channelId", status.runtime.channelId.value()},
        {"stateRevision", status.runtime.stateRevision},
        {"sweepId", std::move(sweepId)},
        {"userPhase", userPhaseName(status.runtime.userPhase)},
        {"progress",
         {{"completedAcquisitionPoints",
           status.runtime.progress.completedPoints},
          {"totalAcquisitionPoints", status.runtime.progress.totalPoints}}},
        {"firstSweepAfterConfiguration",
         status.runtime.firstSweepAfterConfiguration},
        {"activePreviewIdentity", std::move(active)},
    };
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
        {"sweepStatus", sweepStatusToJson(event.status)},
    };
}

Json previewEventToJson(const vna::application::SweepPreviewInvalidated& event) {
    return {
        {"type", "invalidated"},
        {"eventCursor", event.cursor.value},
        {"generation", event.identity.generation},
        {"sweepId", event.identity.sweepId.value()},
        {"sweepStatus", sweepStatusToJson(event.status)},
    };
}

Json previewEventToJson(
    const vna::application::SweepPreviewGenerationAdvanced& event) {
    return {
        {"type", "generationAdvanced"},
        {"eventCursor", event.cursor.value},
        {"generation", event.generation},
        {"sweepStatus", sweepStatusToJson(event.status)},
    };
}

Json previewEventToJson(
    const vna::application::SweepPreviewStatusChanged& event) {
    return {
        {"type", "status"},
        {"eventCursor", event.cursor.value},
        {"sweepStatus", sweepStatusToJson(event.status)},
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
