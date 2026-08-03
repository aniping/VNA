#pragma once

#include <vna/application/command_bus.hpp>

namespace vna::web_api::detail {

// Keep the HTTP status and stable error token in one exhaustive catalog.
struct CommandOutcomeInfo {
    const char* responseStatus;
    int httpStatus;
    const char* errorCode;
};

[[nodiscard]] CommandOutcomeInfo commandOutcomeInfo(
    const application::CommandOutcome& outcome) noexcept;

}  // namespace vna::web_api::detail
