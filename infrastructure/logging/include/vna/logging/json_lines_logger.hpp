#pragma once

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>

#include <vna/observability/logger.hpp>

namespace vna::logging {

struct JsonLinesLoggerOptions {
    std::filesystem::path logDirectory;
    // Non-owning/non-null; must outlive Logger. Its write/flush must return.
    std::ostream* console{&std::cout};
    // Must be > 0; internal flush barriers are coalesced separately.
    std::size_t queueCapacity{1024};
};

// For reliable delivery, the owner must flush after its final submit and get true;
// destruction only performs a best-effort stop and may discard unfinished events.
[[nodiscard]] std::unique_ptr<observability::Logger> makeJsonLinesLogger(
    JsonLinesLoggerOptions options);

}  // namespace vna::logging
