#pragma once

#include <vna/compat/filesystem.hpp>

namespace vna::server {

// Logging is deliberately process-private: callers use the named "vna"
// logger from their own private implementation files.
void initializeRuntimeLog(
    const vna::compat::filesystem::path& releaseRoot) noexcept;
void flushRuntimeLog() noexcept;

}  // namespace vna::server
