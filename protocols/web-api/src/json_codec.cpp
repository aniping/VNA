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

Json channelToJson(const domain::ChannelSnapshot& channel) {
    return {{"id", channel.id.value()}, {"sweep", sweepToJson(channel.sweep)}};
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

const char* traceFormatName(domain::TraceFormat format) {
    switch (format) {
        case domain::TraceFormat::LogMagnitude:
            return "logMagnitude";
        case domain::TraceFormat::Phase:
            return "phase";
        case domain::TraceFormat::Smith:
            return "smith";
    }
    return "unknown";
}

Json traceToJson(const domain::TraceSnapshot& trace) {
    return {
        {"id", trace.id.value()},
        {"windowId", trace.windowId.value()},
        {"measurementId", trace.measurementId.value()},
        {"format", traceFormatName(trace.format)},
    };
}

Json instrumentToJson(const domain::InstrumentSnapshot& instrument) {
    Json channels = Json::array();
    for (const auto& channel : instrument.channels) {
        channels.push_back(channelToJson(channel));
    }
    Json measurements = Json::array();
    for (const auto& measurement : instrument.measurements) {
        measurements.push_back(measurementToJson(measurement));
    }
    Json windows = Json::array();
    for (const auto& window : instrument.windows) {
        windows.push_back({{"id", window.id.value()}});
    }
    Json traces = Json::array();
    for (const auto& trace : instrument.traces) {
        traces.push_back(traceToJson(trace));
    }
    return {
        {"channels", std::move(channels)},
        {"measurements", std::move(measurements)},
        {"windows", std::move(windows)},
        {"traces", std::move(traces)},
    };
}

struct CommandStatusInfo {
    const char* name;
    int httpStatus;
};

CommandStatusInfo commandStatusInfo(const application::CommandOutcome& outcome) {
    if (std::holds_alternative<application::CommandSuccess>(outcome)) {
        return {"succeeded", 200};
    }
    const auto& error = std::get<application::CommandError>(outcome);
    switch (application::commandErrorCode(error)) {
        case application::CommandErrorCode::InvalidSweepSettings:
        case application::CommandErrorCode::ChannelNotFound:
        case application::CommandErrorCode::MeasurementNotFound:
        case application::CommandErrorCode::WindowNotFound:
        case application::CommandErrorCode::TraceNotFound:
            return {"validationError", 422};
        case application::CommandErrorCode::StateRevisionConflict:
            return {"conflict", 409};
        case application::CommandErrorCode::WrongInstrument:
            return {"wrongInstrument", 404};
    }
    return {"unknown", 500};
}

void encodeCommandValue(Json& body, const application::CommandValue& value) {
    if (const auto* channelId = std::get_if<domain::ChannelId>(&value)) {
        body["value"] = {{"channelId", channelId->value()}};
    }
    if (const auto* measurementId =
            std::get_if<domain::MeasurementId>(&value)) {
        body["value"] = {{"measurementId", measurementId->value()}};
    }
    if (const auto* windowId = std::get_if<domain::WindowId>(&value)) {
        body["value"] = {{"windowId", windowId->value()}};
    }
    if (const auto* traceId = std::get_if<domain::TraceId>(&value)) {
        body["value"] = {{"traceId", traceId->value()}};
    }
}

}  // namespace

std::string encodeState(const application::StateSnapshot& state) {
    return Json{
        {"stateRevision", state.stateRevision},
        {"instrument", instrumentToJson(state.instrument)},
    }.dump();
}

CommandResponse encodeCommandResult(
    const application::CommandResult& result) {
    const auto info = commandStatusInfo(result.outcome);
    Json body{
        {"status", info.name},
        {"stateRevision", result.stateRevision},
    };
    if (const auto* success =
            std::get_if<application::CommandSuccess>(&result.outcome)) {
        encodeCommandValue(body, success->value);
    }
    return {info.httpStatus, body.dump()};
}

}  // namespace vna::web_api::detail
