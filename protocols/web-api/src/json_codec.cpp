#include "json_codec.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace vna::web_api::detail {
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

const char* measurementTypeName(domain::MeasurementType type) {
    switch (type) {
        case domain::MeasurementType::S11:
            return "S11";
        case domain::MeasurementType::S21:
            return "S21";
    }
    return "unknown";
}

Json measurementToJson(const domain::MeasurementSnapshot& measurement) {
    return {
        {"id", measurement.id.value()},
        {"channelId", measurement.channelId.value()},
        {"type", measurementTypeName(measurement.type)},
    };
}

const char* traceFormatName(display_model::TraceFormat format) {
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

struct CommandOutcomeInfo {
    const char* status;
    int httpStatus;
    const char* errorCode;
};

CommandOutcomeInfo commandOutcomeInfo(
    const application::CommandOutcome& outcome) {
    if (std::holds_alternative<application::CommandSuccess>(outcome)) {
        return {"succeeded", 200, nullptr};
    }
    const auto& error = std::get<application::CommandError>(outcome);
    switch (application::commandErrorCode(error)) {
        case application::CommandErrorCode::InvalidSweepSettings:
            return {"validationError", 422, "invalid-sweep-settings"};
        case application::CommandErrorCode::ChannelNotFound:
            return {"validationError", 422, "channel-not-found"};
        case application::CommandErrorCode::MeasurementNotFound:
            return {"validationError", 422, "measurement-not-found"};
        case application::CommandErrorCode::WindowNotFound:
            return {"validationError", 422, "window-not-found"};
        case application::CommandErrorCode::TraceNotFound:
            return {"validationError", 422, "trace-not-found"};
        case application::CommandErrorCode::InvalidScalePerDivision:
            return {
                "validationError", 422, "invalid-scale-per-division"};
        case application::CommandErrorCode::ScaleNotSupportedForFormat:
            return {
                "validationError", 422, "scale-not-supported-for-format"};
        case application::CommandErrorCode::CommandIdReuse:
            return {"conflict", 409, "command-id-reuse"};
        case application::CommandErrorCode::ControlDenied:
            return {"conflict", 409, "control-denied"};
        case application::CommandErrorCode::ResourceBusy:
            return {"conflict", 409, "resource-busy"};
        case application::CommandErrorCode::StateRevisionConflict:
            return {"conflict", 409, "state-revision-conflict"};
        case application::CommandErrorCode::UnsupportedSweepConfiguration:
            return {
                "validationError",
                422,
                "unsupported-sweep-configuration"};
        case application::CommandErrorCode::WrongInstrument:
            return {"wrongInstrument", 404, "wrong-instrument"};
    }
    return {"unknown", 500, "unknown"};
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
        {"status", info.status},
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
