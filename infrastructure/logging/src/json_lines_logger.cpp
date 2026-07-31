#include <vna/logging/json_lines_logger.hpp>

#include "json_line_formatter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace vna::logging {
namespace {

struct WorkItem {
    std::uint64_t sequence;
    std::string line;
    std::shared_ptr<std::promise<bool>> completion;
};

class JsonLinesLogger final : public observability::Logger {
public:
    explicit JsonLinesLogger(const JsonLinesLoggerOptions& options)
        : console_(options.console) {
        if (options.logDirectory.empty() || console_ == nullptr) {
            throw std::invalid_argument("invalid JSON Lines logger options");
        }
        std::filesystem::create_directories(options.logDirectory);
        file_.open(
            options.logDirectory / "vna.log.jsonl",
            std::ios::binary | std::ios::app);
        if (!file_) {
            throw std::runtime_error("failed to open JSON Lines log file");
        }
        writer_ = std::thread{[this] { writeLoop(); }};
    }

    ~JsonLinesLogger() override {
        {
            const std::scoped_lock lock{stateMutex_};
            stopping_ = true;
            work_.clear();
        }
        stateChanged_.notify_all();
        writer_.join();
    }

    observability::SubmitResult submit(
        observability::LogEvent event) override {
        auto line = formatJsonLine(event, std::chrono::system_clock::now());
        {
            const std::scoped_lock lock{stateMutex_};
            if (terminal_ || stopping_) {
                return observability::SubmitResult::Stopped;
            }
            const auto sequence = lastAcceptedSequence_ + 1;
            work_.push_back({sequence, std::move(line), {}});
            lastAcceptedSequence_ = sequence;
        }
        stateChanged_.notify_all();
        return observability::SubmitResult::Accepted;
    }

    bool flush(std::chrono::milliseconds timeout) override {
        const auto nonNegative = std::max(timeout, std::chrono::milliseconds{0});
        const auto deadline = std::chrono::steady_clock::now() + nonNegative;
        const auto lockBeforeDeadline = [&](auto& lock) {
            return timeout <= std::chrono::milliseconds{0}
                       ? lock.try_lock()
                       : lock.try_lock_until(deadline);
        };
        std::unique_lock<std::timed_mutex> serial{flushMutex_, std::defer_lock};
        if (!lockBeforeDeadline(serial)) {
            return false;
        }
        std::unique_lock<std::timed_mutex> state{stateMutex_, std::defer_lock};
        if (!lockBeforeDeadline(state) ||
            std::chrono::steady_clock::now() > deadline || terminal_ || stopping_) {
            return false;
        }
        auto completion = std::make_shared<std::promise<bool>>();
        auto completed = completion->get_future();
        work_.push_back({lastAcceptedSequence_, {}, std::move(completion)});
        state.unlock();
        stateChanged_.notify_all();
        if (completed.wait_until(deadline) != std::future_status::ready) {
            return false;
        }
        try {
            return completed.get();
        } catch (...) {
            return false;
        }
    }

    observability::LoggerStatistics statistics() const noexcept override {
        return {sinkFailures_.load(std::memory_order_relaxed)};
    }

private:
    bool write(std::ostream& sink, const std::string& line) noexcept {
        try {
            sink.write(line.data(), static_cast<std::streamsize>(line.size()));
            return sink.good();
        } catch (...) {
            return false;
        }
    }

    bool flush(std::ostream& sink) noexcept {
        try {
            sink.flush();
            return sink.good();
        } catch (...) {
            return false;
        }
    }

    void enterTerminalFailure() noexcept {
        {
            const std::scoped_lock lock{stateMutex_};
            if (terminal_) return;
            terminal_ = true;
            sinkFailures_.fetch_add(1, std::memory_order_relaxed);
            work_.clear();
        }
        stateChanged_.notify_all();
    }

    void completeBarrier(const WorkItem& item) {
        const bool allWritten = writtenSequence_ >= item.sequence;
        const bool sinksOk = allWritten && flush(*console_) && flush(file_);
        if (allWritten && !sinksOk) {
            enterTerminalFailure();
        }
        item.completion->set_value(sinksOk);
    }

    void process(const WorkItem& item) {
        if (item.completion) {
            completeBarrier(item);
            return;
        }
        if (!write(*console_, item.line) || !write(file_, item.line)) {
            enterTerminalFailure();
            return;
        }
        writtenSequence_ = item.sequence;
    }

    void writeLoop() {
        while (true) {
            WorkItem item;
            {
                std::unique_lock lock{stateMutex_};
                stateChanged_.wait(
                    lock,
                    [this] { return stopping_ || terminal_ || !work_.empty(); });
                if (stopping_ || terminal_) {
                    return;
                }
                item = std::move(work_.front());
                work_.pop_front();
            }
            process(item);
        }
    }

    std::ostream* console_;
    std::ofstream file_;
    std::timed_mutex stateMutex_;
    std::timed_mutex flushMutex_;
    std::condition_variable_any stateChanged_;
    std::deque<WorkItem> work_;
    std::thread writer_;
    std::atomic<std::uint64_t> sinkFailures_{0};
    std::uint64_t lastAcceptedSequence_{0};
    std::uint64_t writtenSequence_{0};
    bool terminal_{false};
    bool stopping_{false};
};

}  // namespace

std::unique_ptr<observability::Logger> makeJsonLinesLogger(
    JsonLinesLoggerOptions options) {
    return std::make_unique<JsonLinesLogger>(options);
}

}  // namespace vna::logging
