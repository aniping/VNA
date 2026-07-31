#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace vna::platform {

// Fields that do not apply to the current platform are ignored.
struct StateDirectoryInputs {
    std::optional<std::filesystem::path> localAppData;
    std::optional<std::filesystem::path> xdgStateHome;
    std::optional<std::filesystem::path> homeDirectory;
};

[[nodiscard]] std::filesystem::path resolveStateDirectory(
    std::string_view appName,
    const StateDirectoryInputs& inputs);

[[nodiscard]] std::filesystem::path currentUserStateDirectory(
    std::string_view appName);

}  // namespace vna::platform
