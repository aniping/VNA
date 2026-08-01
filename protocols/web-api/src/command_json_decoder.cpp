#include "json_codec.hpp"
#include "json_value.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace vna::web_api::detail {
namespace {

using Json = nlohmann::json;

std::optional<std::uint64_t> expectedRevision(const Json& request) {
    if (!request.contains("expectedStateRevision") ||
        request.at("expectedStateRevision").is_null()) {
        return std::nullopt;
    }
    return request.at("expectedStateRevision").get<std::uint64_t>();
}

domain::MeasurementType measurementTypeFromJson(
    const Json& payload,
    const char* field) {
    const auto type = payload.at(field).get<std::string>();
    if (type == "S11") {
        return domain::MeasurementType::S11;
    }
    if (type == "S21") {
        return domain::MeasurementType::S21;
    }
    if (type == "S12") {
        return domain::MeasurementType::S12;
    }
    if (type == "S22") {
        return domain::MeasurementType::S22;
    }
    throw std::invalid_argument{"unsupported measurement type"};
}

display_model::TraceFormat traceFormatFromJson(const Json& payload) {
    const auto format = payload.at("format").get<std::string>();
    if (format == "logMagnitude") {
        return display_model::TraceFormat::LogMagnitude;
    }
    if (format == "phase") {
        return display_model::TraceFormat::Phase;
    }
    if (format == "smith") {
        return display_model::TraceFormat::Smith;
    }
    throw std::invalid_argument{"unsupported trace format"};
}

display_model::TraceId traceIdFromJson(const Json& payload) {
    return display_model::TraceId{
        payload.at("traceId").get<std::uint64_t>()};
}

domain::SweepSettings sweepSettingsFromJson(const Json& payload) {
    return {
        .startFrequencyHz = unsignedInteger<std::uint64_t>(
            payload,
            "startFrequencyHz"),
        .stopFrequencyHz = unsignedInteger<std::uint64_t>(
            payload,
            "stopFrequencyHz"),
        .points = unsignedInteger<std::uint32_t>(payload, "points"),
        .ifBandwidthHz = unsignedInteger<std::uint64_t>(
            payload,
            "ifBandwidthHz"),
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
            measurementTypeFromJson(payload, "type")};
    }
    if (type == "createWindow") {
        return application::CreateWindowCommand{};
    }
    if (type == "createTrace") {
        return application::CreateTraceCommand{
            display_model::WindowId{
                payload.at("windowId").get<std::uint64_t>()},
            domain::MeasurementId{
                payload.at("measurementId").get<std::uint64_t>()},
            traceFormatFromJson(payload)};
    }
    if (type == "updateTraceFormat") {
        return application::UpdateTraceFormatCommand{
            traceIdFromJson(payload),
            traceFormatFromJson(payload)};
    }
    if (type == "setTraceMeasurementType") {
        return application::SetTraceMeasurementTypeCommand{
            traceIdFromJson(payload),
            measurementTypeFromJson(payload, "measurementType")};
    }
    if (type == "updateTraceScalePerDivision") {
        return application::UpdateTraceScalePerDivisionCommand{
            traceIdFromJson(payload),
            payload.at("scalePerDivision").get<double>()};
    }
    if (type == "startSingleSweep") {
        return application::StartSingleSweepCommand{
            domain::ChannelId{
                payload.at("channelId").get<std::uint64_t>()}};
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
        .origin = application::CommandOrigin::Web,
        .expectedStateRevision = expectedRevision(request),
        .payload = commandPayloadFromJson(type, payload),
    };
}

}  // namespace

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

}  // namespace vna::web_api::detail
