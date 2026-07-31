#include <vna/platform/state_directory.hpp>

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace vna::platform {
namespace {

void validateApplicationName(std::string_view appName) {
    const std::filesystem::path namePath{appName};
    const bool hasSeparator =
        appName.find_first_of("/\\") != std::string_view::npos;
    if (appName.empty() || appName == "." || appName == ".." ||
        hasSeparator || namePath.is_absolute() || namePath.has_root_name()) {
        throw std::invalid_argument("invalid application name");
    }
}

std::filesystem::path requireAbsolute(
    const std::optional<std::filesystem::path>& value,
    const char* variableName) {
    if (!value) {
        throw std::runtime_error(
            std::string{variableName} + " is not set");
    }
    if (!value->is_absolute()) {
        throw std::invalid_argument(
            std::string{variableName} + " must be absolute");
    }
    return *value;
}

std::optional<std::filesystem::path> environmentPath(const char* name) {
    const auto* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path{value};
}

}  // namespace

std::filesystem::path resolveStateDirectory(
    std::string_view appName,
    const StateDirectoryInputs& inputs) {
    validateApplicationName(appName);
    if (inputs.xdgStateHome) {
        return requireAbsolute(inputs.xdgStateHome, "XDG_STATE_HOME") /
               appName;
    }
    return requireAbsolute(inputs.homeDirectory, "HOME") / ".local" /
           "state" / appName;
}

std::filesystem::path currentUserStateDirectory(std::string_view appName) {
    return resolveStateDirectory(
        appName,
        StateDirectoryInputs{
            .localAppData = std::nullopt,
            .xdgStateHome = environmentPath("XDG_STATE_HOME"),
            .homeDirectory = environmentPath("HOME"),
        });
}

}  // namespace vna::platform
