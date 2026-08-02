#include <vna/server/startup_observability.hpp>

#include <array>
#include <string>
#include <string_view>

namespace vna::server {
namespace {

struct Milestone {
    std::string_view event;
    std::string_view status;
};

bool writeMilestone(
    observability::Logger& logger,
    const Milestone& milestone,
    std::string_view instrumentId,
    observability::LogLevel level = observability::LogLevel::Info) noexcept {
    return logger.write({
        .level = level,
        .name = std::string{milestone.event},
        .commandId = {},
        .sessionId = {},
        .instrumentId = std::string{instrumentId},
        .stateRevision = {},
        .status = std::string{milestone.status},
    });
}

constexpr std::array startupMilestones{
    Milestone{"server.lifecycle", "starting"},
    Milestone{"server.factory_preset", "loaded"},
    Milestone{"server.continuous_acquisition", "running"},
    Milestone{"server.display_publication", "running"},
    Milestone{"server.web_listener", "starting"},
};
constexpr Milestone listenFailed{"server.web_listener", "listen_failed"};
constexpr Milestone stopped{"server.lifecycle", "stopped"};

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
