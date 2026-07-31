#include <vna/platform/state_directory.hpp>

#include <cstdlib>
#include <stdexcept>

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

const std::filesystem::path& requireLocalAppData(
    const StateDirectoryInputs& inputs) {
    if (!inputs.localAppData) {
        throw std::runtime_error("LOCALAPPDATA is not set");
    }
    if (!inputs.localAppData->is_absolute()) {
        throw std::invalid_argument("LOCALAPPDATA must be absolute");
    }
    return *inputs.localAppData;
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
    return requireLocalAppData(inputs) / appName;
}

std::filesystem::path currentUserStateDirectory(std::string_view appName) {
    return resolveStateDirectory(
        appName,
        StateDirectoryInputs{
            .localAppData = environmentPath("LOCALAPPDATA"),
            .xdgStateHome = std::nullopt,
            .homeDirectory = std::nullopt,
        });
}

}  // namespace vna::platform
