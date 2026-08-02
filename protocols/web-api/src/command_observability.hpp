#pragma once

#include <vna/application/command_bus.hpp>
#include <vna/observability/logger.hpp>

namespace vna::web_api::detail {

// Observability is best-effort and never changes the already committed command
// response. The event contains identities and outcome, never the request body.
[[nodiscard]] bool recordWebCommand(
    observability::Logger* logger,
    const application::CommandEnvelope& command,
    const application::CommandResult& result) noexcept;

}  // namespace vna::web_api::detail
