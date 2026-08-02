#include <vna/server/startup_observability.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vna::server {
namespace {

struct Milestone {
    std::string_view event;
    std::string_view status;
};

constexpr std::array milestones{
    Milestone{"server.lifecycle", "starting"},
    Milestone{"server.factory_preset", "loaded"},
    Milestone{"server.continuous_acquisition", "running"},
    Milestone{"server.display_publication", "running"},
    Milestone{"server.web_listener", "starting"},
};

const char* measurementName(domain::MeasurementType type) {
    switch (type) {
    case domain::MeasurementType::S11: return "S11";
    case domain::MeasurementType::S21: return "S21";
    case domain::MeasurementType::S12: return "S12";
    case domain::MeasurementType::S22: return "S22";
    }
    throw std::invalid_argument("unsupported factory measurement type");
}

const char* formatName(display_model::TraceFormat format) {
    switch (format) {
    case display_model::TraceFormat::LogMagnitude: return "Log Magnitude";
    case display_model::TraceFormat::Phase: return "Phase";
    case display_model::TraceFormat::Smith: return "Smith";
    }
    throw std::invalid_argument("unsupported factory trace format");
}

std::string scalar(double value) {
    std::ostringstream output;
    output << value;
    return output.str();
}

std::string frequency(std::uint64_t hertz) {
    if (hertz >= 1'000'000'000ULL) {
        return scalar(static_cast<double>(hertz) / 1.0e9) + " GHz";
    }
    if (hertz >= 1'000'000ULL) {
        return scalar(static_cast<double>(hertz) / 1.0e6) + " MHz";
    }
    if (hertz >= 1'000ULL) {
        return scalar(static_cast<double>(hertz) / 1.0e3) + " kHz";
    }
    return std::to_string(hertz) + " Hz";
}

std::string ports(const std::vector<std::uint32_t>& values) {
    std::string output;
    for (const auto value : values) {
        if (!output.empty()) output += '/';
        output += std::to_string(value);
    }
    return output;
}

struct PresetDisplay {
    domain::MeasurementSnapshot measurement;
    display_model::TraceSnapshot trace;
};

PresetDisplay presetDisplay(const application::FactoryPreset& preset) {
    const auto display = preset.commandBusState.displayWorkspace.snapshot();
    const auto trace = std::find_if(
        display.traces.begin(), display.traces.end(), [&](const auto& value) {
            return value.id == preset.defaultTraceId;
        });
    const auto instrument = preset.commandBusState.instrument.snapshot();
    if (trace == display.traces.end()) {
        throw std::invalid_argument("factory trace is missing");
    }
    const auto measurement = std::find_if(
        instrument.measurements.begin(), instrument.measurements.end(),
        [&](const auto& value) { return value.id == trace->measurementId; });
    if (measurement == instrument.measurements.end()) {
        throw std::invalid_argument("factory measurement is missing");
    }
    return {*measurement, *trace};
}

std::array<std::string, 5> messages(
    const application::FactoryPreset& preset,
    std::string_view webUrl) {
    const auto selected = presetDisplay(preset);
    const auto& plan = preset.acquisitionPlan;
    const auto period = std::chrono::duration_cast<std::chrono::milliseconds>(
        plan.minimumSweepPeriod).count();
    return {
        "Starting Vector Network Analyzer server",
        "Factory preset loaded: Channel " +
            std::to_string(preset.acquisitionChannelId.value()) + ", " +
            measurementName(selected.measurement.type) + ", Trace " +
            std::to_string(selected.trace.id.value()) + ", " +
            std::to_string(plan.frequencyAxis.points) + " points, " +
            frequency(plan.frequencyAxis.startFrequencyHz) + "–" +
            frequency(plan.frequencyAxis.stopFrequencyHz),
        "Continuous acquisition started: " + std::to_string(period) +
            " ms, ports " + ports(plan.sourcePorts) + ", IFBW " +
            frequency(plan.ifBandwidthHz) + ", power " +
            scalar(plan.powerDbm) + " dBm",
        "Live display publication started: Trace " +
            std::to_string(selected.trace.id.value()) + ", " +
            formatName(selected.trace.format),
        "Starting Web service at " + std::string{webUrl},
    };
}

std::string webUrl(std::string_view address, int port) {
    return "http://" + std::string{address} + ':' + std::to_string(port) + '/';
}

bool writeMilestone(
    observability::Logger& logger,
    const Milestone& milestone,
    std::string_view message,
    std::string_view instrumentId,
    observability::LogLevel level = observability::LogLevel::Info) noexcept {
    return logger.write({
        .level = level,
        .name = std::string{milestone.event},
        .message = std::string{message},
        .commandId = {},
        .sessionId = {},
        .instrumentId = std::string{instrumentId},
        .stateRevision = {},
        .status = std::string{milestone.status},
    });
}

}  // namespace

StartupLogDetails::StartupLogDetails(
    std::string instrumentId,
    std::string webUrl,
    std::array<std::string, 5> messages)
    : instrumentId_(std::move(instrumentId)),
      webUrl_(std::move(webUrl)),
      messages_(std::move(messages)) {}

StartupLogDetails makeStartupLogDetails(
    const application::FactoryPreset& preset,
    std::string_view instrumentId,
    std::string_view webAddress,
    int webPort) {
    auto url = webUrl(webAddress, webPort);
    auto startupMessages = messages(preset, url);
    return {std::string{instrumentId}, std::move(url),
            std::move(startupMessages)};
}

bool writeStartupMilestones(
    observability::Logger& logger,
    const StartupLogDetails& details) noexcept {
    for (std::size_t index = 0; index < milestones.size(); ++index) {
        if (!writeMilestone(logger, milestones[index], details.messages_[index],
                            details.instrumentId_)) {
            return false;
        }
    }
    return true;
}

bool writeListenFailed(
    observability::Logger& logger,
    const StartupLogDetails& details) noexcept {
    return writeMilestone(
        logger,
        {"server.web_listener", "listen_failed"},
        "Web service failed to listen at " + details.webUrl_,
        details.instrumentId_,
        observability::LogLevel::Error);
}

bool writeStopped(
    observability::Logger& logger,
    const StartupLogDetails& details) noexcept {
    return writeMilestone(
        logger,
        {"server.lifecycle", "stopped"},
        "Vector Network Analyzer server stopped",
        details.instrumentId_);
}

}  // namespace vna::server
