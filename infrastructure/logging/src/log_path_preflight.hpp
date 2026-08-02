#pragma once

#include <filesystem>

#include <vna/logging/json_lines_logger.hpp>

namespace vna::logging {

std::filesystem::path prepareLogFiles(
    const JsonLinesLoggerOptions& options);

}  // namespace vna::logging
