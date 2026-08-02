#include <vna/logging/json_lines_logger.hpp>

#include "json_line_formatter.hpp"
#include "managed_path.hpp"

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
#include <utility>
#include <vector>

namespace vna::logging {
namespace {

constexpr auto kActiveFilename = "vna.log.jsonl";

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

void validateOptions(const JsonLinesLoggerOptions& options) {
    constexpr auto maxArchives =
        spdlog::sinks::rotating_file_sink_mt::MaxFiles;
    if (options.logDirectory.empty() || options.maxFileBytes == 0 ||
        options.maxFiles == 0 || options.maxFiles - 1 > maxArchives) {
        throw std::invalid_argument("invalid JSON Lines logger options");
    }
}

std::filesystem::path archivePath(
    const std::filesystem::path& active,
    std::size_t index) {
    return active.parent_path() /
        (active.stem().string() + "." + std::to_string(index) +
         active.extension().string());
}

void validateManagedFile(
    const std::filesystem::path& path,
    std::size_t maxFileBytes) {
    const auto kind = classifyManagedPathNoFollow(path);
    if (kind == ManagedPathKind::Unsafe) {
        throw std::runtime_error("JSON Lines log path is not a regular file");
    }
    if (kind == ManagedPathKind::Regular &&
        std::filesystem::file_size(path) > maxFileBytes) {
        throw std::runtime_error("existing JSON Lines log file exceeds limit");
    }
}

std::filesystem::path prepareLogDirectory(
    const JsonLinesLoggerOptions& options) {
    validateOptions(options);
    std::filesystem::create_directories(options.logDirectory);
    const auto active = options.logDirectory / kActiveFilename;
    validateManagedFile(active, options.maxFileBytes);
    for (std::size_t index = 1; index < options.maxFiles; ++index) {
        validateManagedFile(archivePath(active, index), options.maxFileBytes);
    }
    return active;
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

class SpdlogJsonLinesLogger final : public observability::Logger {
public:
    SpdlogJsonLinesLogger(
        const JsonLinesLoggerOptions& options,
        const std::filesystem::path& activePath)
        : maxRecordBytes_(options.maxFileBytes - 1),
          logger_(makeLogger(options, activePath)) {
        logger_->set_error_handler([this](const std::string&) noexcept {
            failed_.store(true, std::memory_order_release);
        });
    }

    bool write(observability::LogEvent event) noexcept override {
        if (failed_.load(std::memory_order_acquire)) return false;
        try {
            const auto record = formatJsonRecord(
                event, std::chrono::system_clock::now());
            if (record.size() > maxRecordBytes_) return false;
            logger_->log(toSpdlogLevel(event.level),
                         spdlog::string_view_t{record.data(), record.size()});
            return !failed_.load(std::memory_order_acquire);
        } catch (...) {
            return false;
        }
    }

    bool flush() noexcept override {
        try {
            logger_->flush();
        } catch (...) {
            failed_.store(true, std::memory_order_release);
        }
        return !failed_.load(std::memory_order_acquire);
    }

private:
    static std::shared_ptr<spdlog::logger> makeLogger(
        const JsonLinesLoggerOptions& options,
        const std::filesystem::path& activePath) {
        std::vector<spdlog::sink_ptr> sinks;
        if (options.console != nullptr) {
            sinks.push_back(
                std::make_shared<CheckedOstreamSink>(*options.console));
        }
        sinks.push_back(
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                activePath.string(), options.maxFileBytes,
                options.maxFiles - 1));
        auto logger = std::make_shared<spdlog::logger>(
            "vna-json-lines", sinks.begin(), sinks.end());
        // JSON owns timestamp and level fields; the sink adds only one LF.
        logger->set_formatter(std::make_unique<spdlog::pattern_formatter>(
            "%v", spdlog::pattern_time_type::utc, "\n"));
        logger->set_level(spdlog::level::debug);
        return logger;
    }

    std::atomic<bool> failed_{false};
    std::size_t maxRecordBytes_;
    std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace

std::unique_ptr<observability::Logger> makeJsonLinesLogger(
    JsonLinesLoggerOptions options) {
    const auto active = prepareLogDirectory(options);
    return std::make_unique<SpdlogJsonLinesLogger>(options, active);
}

}  // namespace vna::logging
