#pragma once

#include <filesystem>

#include <vna/logging/json_lines_logger.hpp>

namespace vna::logging {

struct LogFilePaths {
    std::filesystem::path human;
    std::filesystem::path structured;
};

LogFilePaths prepareLogFiles(
    const JsonLinesLoggerOptions& options);

}  // namespace vna::logging
