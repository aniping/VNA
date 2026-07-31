#include <vna/logging/json_lines_logger.hpp>

#include "json_line_formatter.hpp"
#include "logger_state.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace vna::logging {
namespace {

class JsonLinesLogger final : public observability::Logger {
public:
    explicit JsonLinesLogger(const JsonLinesLoggerOptions& options)
        : sinks_(options.logDirectory, options.console),
          queueCapacity_(options.queueCapacity) {
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
        const bool lowSeverity = isLowSeverity(event.level);
        auto line = formatJsonLine(event, std::chrono::system_clock::now());
        {
            const std::scoped_lock lock{stateMutex_};
            if (terminal_ || stopping_) {
                return observability::SubmitResult::Stopped;
            }
            if (line.size() > kMaxEncodedLineBytes) {
                counters_.rejectOversized();
                return observability::SubmitResult::RejectedOversized;
            }
            if (queuedEvents_ < queueCapacity_) {
                return acceptLocked(std::move(line), lowSeverity);
            }
            if (lowSeverity) {
                counters_.dropLowSeverity();
                return observability::SubmitResult::DroppedLowSeverity;
            }
            const auto oldestLow = findOldestLowSeverity();
            if (oldestLow != work_.end()) {
                recordLoss(oldestLow->sequence);
                work_.erase(oldestLow);
                --queuedEvents_;
                counters_.dropLowSeverity();
                return acceptLocked(std::move(line), false);
            }
        }
        return emergencyFallback(line);
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
        auto completion = pendingBarrier_;
        const bool needsBarrier = !completion ||
            completion->consumed.load(std::memory_order_relaxed) ||
            completion->targetSequence < lastAcceptedSequence_;
        if (needsBarrier) {
            auto signal = std::make_unique<std::promise<bool>>();
            completion = std::make_shared<BarrierCompletion>(
                lastAcceptedSequence_, lossGeneration_,
                signal->get_future().share());
            work_.push_back({completion->targetSequence, {}, completion,
                             std::move(signal), false});
            pendingBarrier_ = completion;
        }
        state.unlock();
        stateChanged_.notify_all();
        if (completion->future.wait_until(deadline) !=
            std::future_status::ready) {
            return false;
        }
        try {
            const bool result = completion->future.get();
            acknowledgedLossGeneration_.store(
                completion->lossGeneration,
                std::memory_order_relaxed);
            completion->consumed.store(true, std::memory_order_relaxed);
            return result;
        } catch (...) {
            return false;
        }
    }

    observability::LoggerStatistics statistics() const noexcept override {
        return counters_.snapshot();
    }

private:
    static bool isLowSeverity(observability::LogLevel level) noexcept {
        return level == observability::LogLevel::Debug ||
               level == observability::LogLevel::Info;
    }

    observability::SubmitResult acceptLocked(std::string line, bool lowSeverity) {
        const auto sequence = ++lastAcceptedSequence_;
        work_.push_back({sequence, std::move(line), {}, {}, lowSeverity});
        ++queuedEvents_;
        stateChanged_.notify_all();
        return observability::SubmitResult::Accepted;
    }

    std::deque<WorkItem>::iterator findOldestLowSeverity() {
        return std::find_if(
            work_.begin(),
            work_.end(),
            [](const WorkItem& item) {
                return !item.completion && item.lowSeverity;
            });
    }

    observability::SubmitResult emergencyFallback(const std::string& line) {
        if (sinks_.writeEmergency(line)) {
            counters_.emergencyFallback();
            return observability::SubmitResult::EmergencyFallback;
        }
        counters_.rejectHighSeverity();
        enterTerminalFailure();
        return observability::SubmitResult::RejectedHighSeverity;
    }

    void recordLoss(std::uint64_t sequence) {
        ++lossGeneration_;
        for (auto& item : work_) {
            if (item.completion && item.sequence >= sequence) {
                item.completion->lossGeneration = lossGeneration_;
            }
        }
    }

    void enterTerminalFailure() noexcept {
        {
            const std::scoped_lock lock{stateMutex_};
            if (terminal_) return;
            terminal_ = true;
            counters_.sinkFailure();
            work_.clear();
        }
        stateChanged_.notify_all();
    }

    void completeBarrier(const WorkItem& item) {
        const bool lost = item.completion->lossGeneration >
            acknowledgedLossGeneration_.load(std::memory_order_relaxed);
        const bool allTerminal = writtenSequence_ >= item.sequence || lost;
        const bool sinksOk = allTerminal && sinks_.flushBoth();
        if (allTerminal && !sinksOk) {
            enterTerminalFailure();
        }
        item.signal->set_value(sinksOk && !lost);
    }

    void process(const WorkItem& item) {
        if (item.completion) {
            completeBarrier(item);
            return;
        }
        if (!sinks_.writeBoth(item.line)) {
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
                if (!item.completion) --queuedEvents_;
            }
            process(item);
        }
    }

    LogSinks sinks_;
    std::timed_mutex stateMutex_;
    std::timed_mutex flushMutex_;
    std::condition_variable_any stateChanged_;
    std::deque<WorkItem> work_;
    std::thread writer_;
    LoggerCounters counters_;
    const std::size_t queueCapacity_;
    std::size_t queuedEvents_{0};
    std::shared_ptr<BarrierCompletion> pendingBarrier_;
    std::atomic<std::uint64_t> acknowledgedLossGeneration_{0};
    std::uint64_t lossGeneration_{0};
    std::uint64_t lastAcceptedSequence_{0};
    std::uint64_t writtenSequence_{0};
    bool terminal_{false};
    bool stopping_{false};
};

}  // namespace

std::unique_ptr<observability::Logger> makeJsonLinesLogger(
    JsonLinesLoggerOptions options) {
    if (options.queueCapacity == 0) {
        throw std::invalid_argument("invalid JSON Lines logger options");
    }
    return std::make_unique<JsonLinesLogger>(options);
}

}  // namespace vna::logging
