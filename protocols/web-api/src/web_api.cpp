#include <vna/web_api/web_api.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

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
}

}  // namespace vna::web_api
