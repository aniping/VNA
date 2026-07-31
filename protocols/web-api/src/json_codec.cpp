#include "json_codec.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <stdexcept>
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

CommandStatusInfo commandStatusInfo(application::CommandStatus status) {
    switch (status) {
        case application::CommandStatus::Succeeded:
            return {"succeeded", 200};
        case application::CommandStatus::ValidationError:
            return {"validationError", 422};
        case application::CommandStatus::Conflict:
            return {"conflict", 409};
        case application::CommandStatus::WrongInstrument:
            return {"wrongInstrument", 404};
    }
    return {"unknown", 500};
}

std::optional<std::uint64_t> expectedRevision(const Json& request) {
    if (!request.contains("expectedStateRevision") ||
        request.at("expectedStateRevision").is_null()) {
        return std::nullopt;
    }
    return request.at("expectedStateRevision").get<std::uint64_t>();
}

domain::MeasurementType measurementTypeFromJson(const Json& payload) {
    const auto type = payload.at("type").get<std::string>();
    if (type == "S11") {
        return domain::MeasurementType::S11;
    }
    if (type == "S21") {
        return domain::MeasurementType::S21;
    }
    throw std::invalid_argument{"unsupported measurement type"};
}

domain::TraceFormat traceFormatFromJson(const Json& payload) {
    const auto format = payload.at("format").get<std::string>();
    if (format == "logMagnitude") {
        return domain::TraceFormat::LogMagnitude;
    }
    if (format == "phase") {
        return domain::TraceFormat::Phase;
    }
    if (format == "smith") {
        return domain::TraceFormat::Smith;
    }
    throw std::invalid_argument{"unsupported trace format"};
}

domain::SweepSettings sweepSettingsFromJson(const Json& payload) {
    return {
        .startFrequencyHz = payload.at("startFrequencyHz").get<std::uint64_t>(),
        .stopFrequencyHz = payload.at("stopFrequencyHz").get<std::uint64_t>(),
        .points = payload.at("points").get<std::uint32_t>(),
        .ifBandwidthHz = payload.at("ifBandwidthHz").get<std::uint64_t>(),
        .powerDbm = payload.at("powerDbm").get<double>(),
    };
}

application::CommandPayload commandPayloadFromJson(
    const std::string& type,
    const Json& payload) {
    if (type == "createChannel") {
        return application::CreateChannelCommand{sweepSettingsFromJson(payload)};
    }
    if (type == "updateChannelSweep") {
        return application::UpdateChannelSweepCommand{
            domain::ChannelId{payload.at("channelId").get<std::uint64_t>()},
            sweepSettingsFromJson(payload)};
    }
    if (type == "createMeasurement") {
        return application::CreateMeasurementCommand{
            domain::ChannelId{payload.at("channelId").get<std::uint64_t>()},
            measurementTypeFromJson(payload)};
    }
    if (type == "createWindow") {
        return application::CreateWindowCommand{};
    }
    if (type == "createTrace") {
        return application::CreateTraceCommand{
            domain::WindowId{payload.at("windowId").get<std::uint64_t>()},
            domain::MeasurementId{
                payload.at("measurementId").get<std::uint64_t>()},
            traceFormatFromJson(payload)};
    }
    throw std::invalid_argument{"unsupported command type"};
}

application::CommandEnvelope commandFromJson(const Json& request) {
    const auto type = request.at("type").get<std::string>();
    const auto& payload = request.at("payload");
    return {
        .commandId = application::CommandId{
            request.at("commandId").get<std::string>()},
        .sessionId = application::SessionId{
            request.at("sessionId").get<std::string>()},
        .instrumentId = application::InstrumentId{
            request.at("instrumentId").get<std::string>()},
        .expectedStateRevision = expectedRevision(request),
        .timeout = std::chrono::seconds{5},
        .priority = application::CommandPriority::Normal,
        .payload = commandPayloadFromJson(type, payload),
    };
}

}  // namespace

std::string encodeState(const application::StateSnapshot& state) {
    return Json{
        {"stateRevision", state.stateRevision},
        {"instrument", instrumentToJson(state.instrument)},
    }.dump();
}

std::optional<application::CommandEnvelope> decodeCommand(
    std::string_view body) {
    try {
        return commandFromJson(Json::parse(body));
    } catch (const Json::exception&) {
        return std::nullopt;
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

CommandResponse encodeCommandResult(
    const application::CommandResult& result) {
    const auto info = commandStatusInfo(result.status);
    Json body{
        {"status", info.name},
        {"stateRevision", result.stateRevision},
    };
    if (const auto* channelId = std::get_if<domain::ChannelId>(&result.value)) {
        body["value"] = {{"channelId", channelId->value()}};
    }
    if (const auto* measurementId =
            std::get_if<domain::MeasurementId>(&result.value)) {
        body["value"] = {{"measurementId", measurementId->value()}};
    }
    if (const auto* windowId = std::get_if<domain::WindowId>(&result.value)) {
        body["value"] = {{"windowId", windowId->value()}};
    }
    if (const auto* traceId = std::get_if<domain::TraceId>(&result.value)) {
        body["value"] = {{"traceId", traceId->value()}};
    }
    return {info.httpStatus, body.dump()};
}

}  // namespace vna::web_api::detail
