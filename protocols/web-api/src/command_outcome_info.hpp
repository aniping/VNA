#pragma once

#include <vna/application/command_bus.hpp>

namespace vna::web_api::detail {

// HTTP encoding and observability share one stable error catalog so a browser
// response and its audit event can never describe different outcomes.
struct CommandOutcomeInfo {
    const char* responseStatus;
    int httpStatus;
    const char* errorCode;
};

[[nodiscard]] CommandOutcomeInfo commandOutcomeInfo(
    const application::CommandOutcome& outcome) noexcept;

}  // namespace vna::web_api::detail
