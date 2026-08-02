#pragma once

#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

namespace vna::test {

// Tests replace only the process-wide named sink used by production. This
// preserves the real private lookup path without adding a logger dependency to
// any public application or protocol constructor.
class CapturedRuntimeLog {
public:
    CapturedRuntimeLog() {
        spdlog::drop("vna");
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_);
        auto logger = std::make_shared<spdlog::logger>("vna", std::move(sink));
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("%l %v");
        spdlog::register_logger(std::move(logger));
    }

    ~CapturedRuntimeLog() { spdlog::drop("vna"); }

    [[nodiscard]] std::string text() const { return stream_.str(); }
    [[nodiscard]] std::size_t count(std::string_view needle) const {
        const auto contents = text();
        std::size_t result = 0;
        for (auto offset = contents.find(needle); offset != std::string::npos;
             offset = contents.find(needle, offset + needle.size())) {
            ++result;
        }
        return result;
    }
    void clear() {
        stream_.str({});
        stream_.clear();
    }

private:
    std::ostringstream stream_;
};

}  // namespace vna::test
