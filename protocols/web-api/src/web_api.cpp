#include <vna/web_api/web_api.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace vna::web_api {
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

Json stateToJson(const application::StateSnapshot& state) {
    return {
        {"stateRevision", state.stateRevision},
        {"instrument", instrumentToJson(state.instrument)},
    };
}

const char* commandStatusName(application::CommandStatus status) {
    switch (status) {
        case application::CommandStatus::Succeeded:
            return "succeeded";
        case application::CommandStatus::ValidationError:
            return "validationError";
        case application::CommandStatus::Conflict:
            return "conflict";
        case application::CommandStatus::WrongInstrument:
            return "wrongInstrument";
    }
    return "unknown";
}

int commandHttpStatus(application::CommandStatus status) {
    switch (status) {
        case application::CommandStatus::Succeeded:
            return httplib::StatusCode::OK_200;
        case application::CommandStatus::ValidationError:
            return httplib::StatusCode::UnprocessableContent_422;
        case application::CommandStatus::Conflict:
            return httplib::StatusCode::Conflict_409;
        case application::CommandStatus::WrongInstrument:
            return httplib::StatusCode::NotFound_404;
    }
    return httplib::StatusCode::InternalServerError_500;
}

Json commandResultToJson(const application::CommandResult& result) {
    Json body{
        {"status", commandStatusName(result.status)},
        {"stateRevision", result.stateRevision},
    };
    if (const auto* channelId =
            std::get_if<domain::ChannelId>(&result.value)) {
        body["value"] = {{"channelId", channelId->value()}};
    }
    return body;
}

std::optional<std::uint64_t> expectedRevision(const Json& request) {
    if (!request.contains("expectedStateRevision") ||
        request.at("expectedStateRevision").is_null()) {
        return std::nullopt;
    }
    return request.at("expectedStateRevision").get<std::uint64_t>();
}

application::CommandEnvelope commandFromJson(const Json& request) {
    if (request.at("type").get<std::string>() != "createChannel") {
        throw std::invalid_argument{"unsupported command type"};
    }
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
        .payload = application::CreateChannelCommand{domain::SweepSettings{
            .startFrequencyHz =
                payload.at("startFrequencyHz").get<std::uint64_t>(),
            .stopFrequencyHz =
                payload.at("stopFrequencyHz").get<std::uint64_t>(),
            .points = payload.at("points").get<std::uint32_t>(),
            .ifBandwidthHz =
                payload.at("ifBandwidthHz").get<std::uint64_t>(),
            .powerDbm = payload.at("powerDbm").get<double>(),
        }},
    };
}

void handleCommand(
    application::CommandBus& commandBus,
    const httplib::Request& request,
    httplib::Response& response) {
    try {
        const auto command = commandFromJson(Json::parse(request.body));
        const auto result = commandBus.dispatch(command);
        response.status = commandHttpStatus(result.status);
        response.set_content(commandResultToJson(result).dump(), "application/json");
    } catch (const Json::exception&) {
        response.status = httplib::StatusCode::BadRequest_400;
        response.set_content(R"({"error":"invalidCommand"})", "application/json");
    } catch (const std::invalid_argument&) {
        response.status = httplib::StatusCode::BadRequest_400;
        response.set_content(R"({"error":"invalidCommand"})", "application/json");
    }
}

}  // namespace

WebApi::WebApi(application::CommandBus& commandBus)
    : commandBus_(commandBus) {}

void WebApi::install(httplib::Server& server) {
    server.Get(
        "/api/v1/health",
        [](const httplib::Request&, httplib::Response& response) {
            response.set_content(R"({"status":"ok"})", "application/json");
        });
    server.Get(
        "/api/v1/state",
        [this](const httplib::Request&, httplib::Response& response) {
            response.set_content(
                stateToJson(commandBus_.snapshot()).dump(),
                "application/json");
        });
    server.Post(
        "/api/v1/commands",
        [this](const httplib::Request& request, httplib::Response& response) {
            handleCommand(commandBus_, request, response);
        });
}

}  // namespace vna::web_api
