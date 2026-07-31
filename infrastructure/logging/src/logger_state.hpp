#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

#include <vna/observability/logger.hpp>

namespace vna::logging {

inline constexpr std::size_t kMaxEncodedLineBytes = 64 * 1024;

struct BarrierCompletion {
    BarrierCompletion(
        std::uint64_t target,
        std::uint64_t loss,
        std::shared_future<bool> completed)
        : targetSequence(target),
          lossGeneration(loss),
          future(std::move(completed)) {}

    std::uint64_t targetSequence;
    std::uint64_t lossGeneration;
    std::shared_future<bool> future;
    std::atomic<bool> consumed{false};
};

struct WorkItem {
    std::uint64_t sequence;
    std::string line;
    std::shared_ptr<BarrierCompletion> completion;
    std::unique_ptr<std::promise<bool>> signal;
    bool lowSeverity;
};

class LogSinks {
public:
    LogSinks(const std::filesystem::path& directory, std::ostream* console)
        : console_(console) {
        if (directory.empty() || console_ == nullptr) {
            throw std::invalid_argument("invalid JSON Lines logger options");
        }
        std::filesystem::create_directories(directory);
        file_.open(
            directory / "vna.log.jsonl",
            std::ios::binary | std::ios::app);
        if (!file_) {
            throw std::runtime_error("failed to open JSON Lines log file");
        }
    }

    bool writeBoth(const std::string& line) noexcept {
        return writeConsole(line) && write(file_, line);
    }

    bool flushBoth() noexcept {
        return flushConsole() && flush(file_);
    }

    bool writeEmergency(const std::string& line) noexcept {
        const std::scoped_lock lock{consoleMutex_};
        return write(*console_, line) && flush(*console_);
    }

private:
    static bool write(std::ostream& sink, const std::string& line) noexcept {
        try {
            sink.write(line.data(), static_cast<std::streamsize>(line.size()));
            return sink.good();
        } catch (...) {
            return false;
        }
    }

    static bool flush(std::ostream& sink) noexcept {
        try {
            sink.flush();
            return sink.good();
        } catch (...) {
            return false;
        }
    }

    bool writeConsole(const std::string& line) noexcept {
        const std::scoped_lock lock{consoleMutex_};
        return write(*console_, line);
    }

    bool flushConsole() noexcept {
        const std::scoped_lock lock{consoleMutex_};
        return flush(*console_);
    }

    std::ostream* console_;
    std::ofstream file_;
    std::recursive_mutex consoleMutex_;
};

class LoggerCounters {
public:
    void dropLowSeverity() noexcept { droppedLowSeverity_.fetch_add(1, std::memory_order_relaxed); }
    void emergencyFallback() noexcept { emergencyFallbacks_.fetch_add(1, std::memory_order_relaxed); }
    void rejectHighSeverity() noexcept { rejectedHighSeverity_.fetch_add(1, std::memory_order_relaxed); }
    void rejectOversized() noexcept { rejectedOversized_.fetch_add(1, std::memory_order_relaxed); }
    void sinkFailure() noexcept { sinkFailures_.fetch_add(1, std::memory_order_relaxed); }

    [[nodiscard]] observability::LoggerStatistics snapshot() const noexcept {
        return {
            droppedLowSeverity_.load(std::memory_order_relaxed),
            emergencyFallbacks_.load(std::memory_order_relaxed),
            rejectedHighSeverity_.load(std::memory_order_relaxed),
            rejectedOversized_.load(std::memory_order_relaxed),
            sinkFailures_.load(std::memory_order_relaxed),
        };
    }

private:
    std::atomic<std::uint64_t> droppedLowSeverity_{0};
    std::atomic<std::uint64_t> emergencyFallbacks_{0};
    std::atomic<std::uint64_t> rejectedHighSeverity_{0};
    std::atomic<std::uint64_t> rejectedOversized_{0};
    std::atomic<std::uint64_t> sinkFailures_{0};
};

}  // namespace vna::logging
