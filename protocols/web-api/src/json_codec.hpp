#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <vna/application/command_bus.hpp>

namespace vna::web_api::detail {

struct CommandResponse {
    int httpStatus;
    std::string body;
};

std::string encodeState(const application::StateSnapshot& state);
std::optional<application::CommandEnvelope> decodeCommand(
    std::string_view body);
CommandResponse encodeCommandResult(
    const application::CommandResult& result);

}  // namespace vna::web_api::detail
