#include "runtime_log.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace vna::server {
namespace {

constexpr auto loggerName = "vna";
constexpr auto filePattern = "%Y-%m-%d %H:%M:%S.%e  %-8l %v";
// Color markers carry no text. On a real Windows console they select the wide
// WriteConsoleW path; redirected output remains the same UTF-8 byte sequence.
constexpr auto consolePattern = "%Y-%m-%d %H:%M:%S.%e  %^%-8l%$ %v";
constexpr std::size_t maxFileBytes = 10U * 1024U * 1024U;
constexpr std::size_t archiveCount = 4U;

using ConsoleSink = spdlog::sinks::stdout_color_sink_mt;

std::shared_ptr<spdlog::logger> makeLogger(
    std::vector<spdlog::sink_ptr> sinks) {
    auto logger = std::make_shared<spdlog::logger>(
        loggerName, sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    spdlog::register_logger(logger);
    return logger;
}

std::shared_ptr<ConsoleSink> makeConsoleSink() {
    auto sink = std::make_shared<ConsoleSink>();
    sink->set_pattern(consolePattern);
    return sink;
}

void reportFileFallback() noexcept {
    std::fputs(
        "Runtime log file is unavailable; continuing with console only.\n",
        stderr);
}

void initializeWithFile(
    const vna::compat::filesystem::path& releaseRoot,
    const std::shared_ptr<ConsoleSink>& console) {
    const auto logDirectory = releaseRoot / "logs";
    vna::compat::filesystem::create_directories(logDirectory);
    auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        (logDirectory / "vna.log").native(),
        maxFileBytes,
        archiveCount);
    file->set_pattern(filePattern);
    static_cast<void>(makeLogger({std::move(file), console}));
}

void initializeConsoleOnly(const std::shared_ptr<ConsoleSink>& console) {
    const auto logger = makeLogger({console});
    logger->warn("无法写入日志文件，将仅输出到控制台");
    reportFileFallback();
}

}  // namespace

void initializeRuntimeLog(
    const vna::compat::filesystem::path& releaseRoot) noexcept {
    try {
        const auto console = makeConsoleSink();
        try {
            initializeWithFile(releaseRoot, console);
        } catch (...) {
            initializeConsoleOnly(console);
        }
    } catch (...) {
        std::fputs("Runtime logging is unavailable; continuing without it.\n", stderr);
    }
}

void flushRuntimeLog() noexcept {
    // Shutdown logging remains best-effort and cannot turn a completed server
    // result into process termination.
    try {
        if (const auto logger = spdlog::get(loggerName)) {
            logger->flush();
        }
    } catch (...) {}
}

}  // namespace vna::server
