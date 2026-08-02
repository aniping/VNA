#pragma once

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>

#include <vna/observability/logger.hpp>

namespace vna::logging {

struct JsonLinesLoggerOptions {
    std::filesystem::path logDirectory;
    // Non-owning; when non-null it must outlive Logger and its I/O must return.
    // nullptr disables ordinary console JSON while retaining the file sink.
    std::ostream* console{&std::cout};
    // Both must be > 0; maxFiles includes the active log file.
    std::size_t maxFileBytes{10U * 1024U * 1024U};
    std::size_t maxFiles{10};
};

// Construction validates options and managed paths before opening the active file.
[[nodiscard]] std::unique_ptr<observability::Logger> makeJsonLinesLogger(
    JsonLinesLoggerOptions options);

}  // namespace vna::logging
