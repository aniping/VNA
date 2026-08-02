#pragma once

#include <array>
#include <string>
#include <string_view>

#include <vna/application/factory_preset.hpp>
#include <vna/observability/logger.hpp>

namespace vna::server {

class StartupLogDetails final {
public:
    StartupLogDetails(StartupLogDetails&&) noexcept = default;
    StartupLogDetails& operator=(StartupLogDetails&&) noexcept = default;

private:
    friend StartupLogDetails makeStartupLogDetails(
        const application::FactoryPreset&,
        std::string_view,
        std::string_view,
        int);
    friend bool writeStartupMilestones(
        observability::Logger&, const StartupLogDetails&) noexcept;
    friend bool writeListenFailed(
        observability::Logger&, const StartupLogDetails&) noexcept;
    friend bool writeStopped(
        observability::Logger&, const StartupLogDetails&) noexcept;

    StartupLogDetails(
        std::string instrumentId,
        std::string webUrl,
        std::array<std::string, 5> messages);

    std::string instrumentId_;
    std::string webUrl_;
    std::array<std::string, 5> messages_;
};

// Capture the preset before composition moves its state into runtime owners.
[[nodiscard]] StartupLogDetails makeStartupLogDetails(
    const application::FactoryPreset& preset,
    std::string_view instrumentId,
    std::string_view webAddress,
    int webPort);

// Reports already-constructed startup stages in their logical product order.
// The listener milestone says starting because listen() has not returned yet.
[[nodiscard]] bool writeStartupMilestones(
    observability::Logger& logger,
    const StartupLogDetails& details) noexcept;
[[nodiscard]] bool writeListenFailed(
    observability::Logger& logger,
    const StartupLogDetails& details) noexcept;
[[nodiscard]] bool writeStopped(
    observability::Logger& logger,
    const StartupLogDetails& details) noexcept;

}  // namespace vna::server
