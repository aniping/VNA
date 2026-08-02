#pragma once

#include <string_view>

#include <vna/observability/logger.hpp>

namespace vna::server {

// Reports already-constructed startup stages in their logical product order.
// The listener milestone says starting because listen() has not returned yet.
[[nodiscard]] bool writeStartupMilestones(
    observability::Logger& logger,
    std::string_view instrumentId) noexcept;
[[nodiscard]] bool writeListenFailed(
    observability::Logger& logger,
    std::string_view instrumentId) noexcept;
[[nodiscard]] bool writeStopped(
    observability::Logger& logger,
    std::string_view instrumentId) noexcept;

}  // namespace vna::server
