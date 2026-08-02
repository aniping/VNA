#pragma once

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>

#include <vna/observability/logger.hpp>

namespace vna::logging {

enum class ConsoleFormat {
    JsonLines,
    HumanReadable,
};

struct JsonLinesLoggerOptions {
    std::filesystem::path logDirectory;
    // Non-owning; when non-null it must outlive Logger and its I/O must return.
    // nullptr disables ordinary console output while retaining the file sink.
    std::ostream* console{&std::cout};
    // JSON remains the compatibility default; portable release selects human.
    ConsoleFormat consoleFormat{ConsoleFormat::JsonLines};
    // Both must be > 0; maxFiles applies to each file family and includes active.
    std::size_t maxFileBytes{10U * 1024U * 1024U};
    std::size_t maxFiles{10};
};

// Construction validates options and managed paths before opening the active file.
[[nodiscard]] std::unique_ptr<observability::Logger> makeJsonLinesLogger(
    JsonLinesLoggerOptions options);

}  // namespace vna::logging
