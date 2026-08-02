#include <vna/server/startup_observability.hpp>

#include <array>
#include <string>
#include <string_view>

namespace vna::server {
namespace {

struct Milestone {
    std::string_view event;
    std::string_view status;
    std::string_view message;
};

bool writeMilestone(
    observability::Logger& logger,
    const Milestone& milestone,
    std::string_view instrumentId,
    observability::LogLevel level = observability::LogLevel::Info) noexcept {
    return logger.write({
        .level = level,
        .name = std::string{milestone.event},
        .message = std::string{milestone.message},
        .commandId = {},
        .sessionId = {},
        .instrumentId = std::string{instrumentId},
        .stateRevision = {},
        .status = std::string{milestone.status},
    });
}

constexpr std::array startupMilestones{
    Milestone{"server.lifecycle", "starting",
              "Starting Vector Network Analyzer server"},
    Milestone{"server.factory_preset", "loaded", "Factory preset loaded"},
    Milestone{"server.continuous_acquisition", "running",
              "Continuous acquisition started"},
    Milestone{"server.display_publication", "running",
              "Live display publication started"},
    Milestone{"server.web_listener", "starting", "Starting Web service"},
};
constexpr Milestone listenFailed{
    "server.web_listener", "listen_failed", "Web service failed to listen"};
constexpr Milestone stopped{
    "server.lifecycle", "stopped", "Vector Network Analyzer server stopped"};

}  // namespace

bool writeStartupMilestones(
    observability::Logger& logger,
    std::string_view instrumentId) noexcept {
    for (const auto& milestone : startupMilestones) {
        if (!writeMilestone(logger, milestone, instrumentId)) return false;
    }
    return true;
}

bool writeListenFailed(
    observability::Logger& logger,
    std::string_view instrumentId) noexcept {
    return writeMilestone(
        logger, listenFailed, instrumentId, observability::LogLevel::Error);
}

bool writeStopped(
    observability::Logger& logger,
    std::string_view instrumentId) noexcept {
    return writeMilestone(logger, stopped, instrumentId);
}

}  // namespace vna::server
