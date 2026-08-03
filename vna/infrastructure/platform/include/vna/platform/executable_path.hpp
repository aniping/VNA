#pragma once

#include <vna/compat/filesystem.hpp>

namespace vna::platform {

// Returns the operating system's path for the running executable. Release
// layout discovery must use this anchor rather than the caller's working
// directory, which is controlled by launchers and users.
[[nodiscard]] vna::compat::filesystem::path currentExecutablePath();

}  // namespace vna::platform
