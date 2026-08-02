#include "json_codec.hpp"
#include "command_outcome_info.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace vna::web_api::detail {
const char* measurementTypeName(domain::MeasurementType type) noexcept {
    switch (type) {
        case domain::MeasurementType::S11:
            return "S11";
        case domain::MeasurementType::S21:
            return "S21";
        case domain::MeasurementType::S12:
            return "S12";
        case domain::MeasurementType::S22:
            return "S22";
    }
    return "unknown";
}

const char* traceFormatName(display_model::TraceFormat format) noexcept {
    switch (format) {
        case display_model::TraceFormat::LogMagnitude:
            return "logMagnitude";
        case display_model::TraceFormat::Phase:
            return "phase";
        case display_model::TraceFormat::Smith:
            return "smith";
    }
    return "unknown";
}

namespace {

using Json = nlohmann::json;

Json sweepToJson(const domain::SweepSettings& sweep) {
    return {
        {"startFrequencyHz", sweep.startFrequencyHz},
        {"stopFrequencyHz", sweep.stopFrequencyHz},
        {"points", sweep.points},
        {"ifBandwidthHz", sweep.ifBandwidthHz},
        {"powerDbm", sweep.powerDbm},
    };
}

const char* sweepModeName(domain::SweepMode mode) {
    switch (mode) {
        case domain::SweepMode::Continuous:
            return "continuous";
    }
    return "unknown";
}

const char* triggerSourceName(domain::TriggerSource source) {
    switch (source) {
        case domain::TriggerSource::None:
            return "none";
    }
    return "unknown";
}

Json channelToJson(const domain::ChannelSnapshot& channel) {
    return {
        {"id", channel.id.value()},
        {"sweep", sweepToJson(channel.sweep)},
        {"sweepMode", sweepModeName(channel.sweepMode)},
        {"triggerSource", triggerSourceName(channel.triggerSource)},
    };
}

Json measurementToJson(const domain::MeasurementSnapshot& measurement) {
    return {
        {"id", measurement.id.value()},
        {"channelId", measurement.channelId.value()},
        {"type", measurementTypeName(measurement.type)},
    };
}

const char* scaleUnitName(display_model::ScaleUnit unit) {
    switch (unit) {
        case display_model::ScaleUnit::Decibel:
            return "dB";
    }
    return "unknown";
}

Json scaleToJson(const display_model::CartesianScaleSnapshot& scale) {
    return {
        {"scalePerDivision", scale.scalePerDivision},
        {"referenceValue", scale.referenceValue},
        {"referencePosition", scale.referencePosition},
        {"minimum", scale.minimum},
        {"maximum", scale.maximum},
        {"unit", scaleUnitName(scale.unit)},
    };
}

Json traceToJson(const display_model::TraceSnapshot& trace) {
    Json scale = nullptr;
    if (trace.scale.has_value()) {
        scale = scaleToJson(*trace.scale);
    }
    return {
        {"id", trace.id.value()},
        {"windowId", trace.windowId.value()},
        {"measurementId", trace.measurementId.value()},
        {"format", traceFormatName(trace.format)},
        {"scale", std::move(scale)},
    };
}

Json instrumentToJson(
    const domain::InstrumentSnapshot& instrument,
    const display_model::DisplayWorkspaceSnapshot& display) {
    Json channels = Json::array();
    for (const auto& channel : instrument.channels) {
        channels.push_back(channelToJson(channel));
    }
    Json measurements = Json::array();
    for (const auto& measurement : instrument.measurements) {
        measurements.push_back(measurementToJson(measurement));
    }
    Json windows = Json::array();
    for (const auto& window : display.windows) {
        windows.push_back({{"id", window.id.value()}});
    }
    Json traces = Json::array();
    for (const auto& trace : display.traces) {
        traces.push_back(traceToJson(trace));
    }
    return {
        {"channels", std::move(channels)},
        {"measurements", std::move(measurements)},
        {"windows", std::move(windows)},
        {"traces", std::move(traces)},
    };
}

void encodeCommandValue(Json& body, const application::CommandValue& value) {
    if (const auto* channelId = std::get_if<domain::ChannelId>(&value)) {
        body["value"] = {{"channelId", channelId->value()}};
    }
    if (const auto* measurementId =
            std::get_if<domain::MeasurementId>(&value)) {
        body["value"] = {{"measurementId", measurementId->value()}};
    }
    if (const auto* windowId =
            std::get_if<display_model::WindowId>(&value)) {
        body["value"] = {{"windowId", windowId->value()}};
    }
    if (const auto* traceId = std::get_if<display_model::TraceId>(&value)) {
        body["value"] = {{"traceId", traceId->value()}};
    }
    if (const auto* operationId =
            std::get_if<application::OperationId>(&value)) {
        body["value"] = {{"operationId", operationId->value()}};
    }
}

}  // namespace

std::string encodeState(const application::StateSnapshot& state) {
    return Json{
        {"stateRevision", state.stateRevision},
        {"instrument", instrumentToJson(state.instrument, state.display)},
    }.dump();
}

CommandResponse encodeCommandResult(
    const application::CommandResult& result) {
    const auto info = commandOutcomeInfo(result.outcome);
    Json body{
        {"status", info.responseStatus},
        {"stateRevision", result.stateRevision},
    };
    if (const auto* success =
            std::get_if<application::CommandSuccess>(&result.outcome)) {
        encodeCommandValue(body, success->value);
    }
    if (info.errorCode != nullptr) {
        body["errorCode"] = info.errorCode;
    }
    return {info.httpStatus, body.dump()};
}

}  // namespace vna::web_api::detail
