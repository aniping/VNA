#pragma once

#include <vna/application/command_bus.hpp>

namespace vna::web_api::detail {

// Records one decoded command attempt after CommandBus has returned. The
// implementation is best-effort and never changes the protocol response.
void logBusinessCommand(
    const application::CommandEnvelope& command,
    const application::CommandResult& result) noexcept;

}  // namespace vna::web_api::detail
