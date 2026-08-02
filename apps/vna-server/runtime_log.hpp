#pragma once

#include <filesystem>

namespace vna::server {

// Logging is deliberately process-private: callers use the named "vna"
// logger from their own private implementation files.
void initializeRuntimeLog(const std::filesystem::path& releaseRoot) noexcept;
void flushRuntimeLog() noexcept;

}  // namespace vna::server
