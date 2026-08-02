#include <vna/logging/json_lines_logger.hpp>

#include "human_log_formatter.hpp"
#include "json_line_formatter.hpp"
#include "log_path_preflight.hpp"

#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vna::logging {
namespace {

class CheckedOstreamSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit CheckedOstreamSink(std::ostream& stream) : stream_(stream) {}

protected:
    void sink_it_(const spdlog::details::log_msg& message) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(message, formatted);
        stream_.write(formatted.data(),
                      static_cast<std::streamsize>(formatted.size()));
        ensureHealthy();
    }

    void flush_() override {
        stream_.flush();
        ensureHealthy();
    }

private:
    void ensureHealthy() const {
        if (!stream_.good()) {
            throw std::runtime_error("JSON Lines console sink failed");
        }
    }

    std::ostream& stream_;
};

std::unique_ptr<spdlog::formatter> jsonLineFormatter() {
    // JSON owns timestamp and level fields; the sink adds only one LF.
    return std::make_unique<spdlog::pattern_formatter>(
        "%v", spdlog::pattern_time_type::utc, "\n");
}

std::shared_ptr<spdlog::logger> makeLogger(
    std::string name,
    spdlog::sink_ptr sink) {
    auto logger = std::make_shared<spdlog::logger>(
        std::move(name), std::move(sink));
    logger->set_level(spdlog::level::debug);
    return logger;
}

std::vector<std::shared_ptr<spdlog::logger>> makeLoggers(
    const JsonLinesLoggerOptions& options,
    const LogFilePaths& paths) {
    // One sink per logger lets write/flush attempt the other destinations even
    // after spdlog reports a failure from one independently rolling file.
    std::vector<std::shared_ptr<spdlog::logger>> loggers;
    auto humanFile = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        paths.human.native(), options.maxFileBytes, options.maxFiles - 1);
    humanFile->set_formatter(makeHumanLogFormatter());
    loggers.push_back(makeLogger("vna-human-file", std::move(humanFile)));

    auto structured = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        paths.structured.native(), options.maxFileBytes, options.maxFiles - 1);
    structured->set_formatter(jsonLineFormatter());
    loggers.push_back(makeLogger("vna-json-file", std::move(structured)));

    if (options.console != nullptr) {
        auto console = std::make_shared<CheckedOstreamSink>(*options.console);
        if (options.consoleFormat == ConsoleFormat::HumanReadable) {
            console->set_formatter(makeHumanLogFormatter());
        } else {
            console->set_formatter(jsonLineFormatter());
        }
        loggers.push_back(makeLogger("vna-console", std::move(console)));
    }
    return loggers;
}

spdlog::level::level_enum toSpdlogLevel(
    observability::LogLevel level) noexcept {
    using observability::LogLevel;
    switch (level) {
    case LogLevel::Debug: return spdlog::level::debug;
    case LogLevel::Info: return spdlog::level::info;
    case LogLevel::Warning: return spdlog::level::warn;
    case LogLevel::Error: return spdlog::level::err;
    }
    return spdlog::level::off;
}

bool isSafeHumanMessage(std::string_view message) noexcept {
    if (message.empty()) return false;
    for (const unsigned char value : message) {
        if (value <= 0x1fU || value == 0x7fU) return false;
    }
    return true;
}

class SpdlogJsonLinesLogger final : public observability::Logger {
public:
    SpdlogJsonLinesLogger(
        const JsonLinesLoggerOptions& options,
        const LogFilePaths& paths)
        : maxRecordBytes_(options.maxFileBytes - 1),
          loggers_(makeLoggers(options, paths)) {
        for (const auto& logger : loggers_) {
            logger->set_error_handler([this](const std::string&) noexcept {
                failed_.store(true, std::memory_order_release);
            });
        }
    }

    bool write(observability::LogEvent event) noexcept override {
        if (failed_.load(std::memory_order_acquire)) return false;
        try {
            if (!isSafeHumanMessage(event.message)) return false;
            const auto timestamp = std::chrono::system_clock::now();
            const auto record = formatJsonRecord(event, timestamp);
            if (record.size() > maxRecordBytes_) return false;
            for (const auto& logger : loggers_) {
                logger->log(timestamp, spdlog::source_loc{},
                            toSpdlogLevel(event.level),
                            spdlog::string_view_t{
                                record.data(), record.size()});
            }
            return !failed_.load(std::memory_order_acquire);
        } catch (...) {
            return false;
        }
    }

    bool flush() noexcept override {
        for (const auto& logger : loggers_) {
            try {
                logger->flush();
            } catch (...) {
                failed_.store(true, std::memory_order_release);
            }
        }
        return !failed_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> failed_{false};
    std::size_t maxRecordBytes_;
    std::vector<std::shared_ptr<spdlog::logger>> loggers_;
};

}  // namespace

std::unique_ptr<observability::Logger> makeJsonLinesLogger(
    JsonLinesLoggerOptions options) {
    const auto paths = prepareLogFiles(options);
    return std::make_unique<SpdlogJsonLinesLogger>(options, paths);
}

}  // namespace vna::logging
