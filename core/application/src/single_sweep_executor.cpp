#include <vna/application/single_sweep_executor.hpp>

#include <condition_variable>
#include <deque>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "single_sweep_pipeline_internal.hpp"

namespace vna::application {

class SingleSweepExecutor::Impl {
public:
    Impl(
        std::size_t capacity,
        RawSweepSource source,
        OperationManager& operations,
        TraceDisplayFrameRepository& frames)
        : capacity_(capacity),
          source_(std::move(source)),
          operations_(operations),
          frames_(frames),
          worker_([this](std::stop_token token) { workerLoop(token); }) {}

    SingleSweepSubmitResult submit(SingleSweepWorkItem work) {
        std::lock_guard lock{mutex_};
        if (!accepting_) {
            return SingleSweepSubmitError{
                .code = SingleSweepSubmitErrorCode::Stopped};
        }
        if (queue_.size() >= capacity_) {
            return SingleSweepSubmitError{
                .code = SingleSweepSubmitErrorCode::QueueFull};
        }
        // Queue allocation happens first, but the worker cannot observe it
        // before unlock/notify. Rollback closes OperationManager::create's
        // exceptional boundary without leaving an id-zero Pending behind.
        queue_.push_back(Pending{.work = std::move(work)});
        try {
            const auto& queued = queue_.back().work;
            auto operation = operations_.create(OperationSubmission{
                queued.commandId,
                queued.sessionId,
                queued.frameContext.stateRevision});
            queue_.back().operationId = operation.id;
            condition_.notify_one();
            return operation;
        } catch (...) {
            queue_.pop_back();
            throw;
        }
    }

    void stop() {
        std::deque<Pending> abandoned;
        std::optional<OperationId> running;
        {
            std::lock_guard lock{mutex_};
            accepting_ = false;
            worker_.request_stop();
            abandoned.swap(queue_);
            running = running_;
        }
        // Operation completion can invoke external fence callbacks, so no
        // executor lock crosses cancellation or join boundaries.
        condition_.notify_all();
        for (const auto& pending : abandoned) {
            cancel(pending.operationId);
        }
        if (running) {
            (void)operations_.requestCancel(*running);
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    struct Pending {
        SingleSweepWorkItem work;
        OperationId operationId{0};
    };

    void workerLoop(std::stop_token token) {
        while (true) {
            std::optional<Pending> pending;
            {
                std::unique_lock lock{mutex_};
                condition_.wait(lock, [&] {
                    return token.stop_requested() || !queue_.empty();
                });
                if (queue_.empty()) {
                    return;
                }
                pending.emplace(std::move(queue_.front()));
                queue_.pop_front();
                running_ = pending->operationId;
            }
            run(std::move(*pending), token);
            std::lock_guard lock{mutex_};
            running_.reset();
        }
    }

    void run(Pending pending, std::stop_token token) {
        if (finishCancellation(pending.operationId, token)) {
            return;
        }
        const auto running = operations_.markRunning(pending.operationId);
        if (!std::holds_alternative<OperationSnapshot>(running)) {
            (void)finishCancellation(pending.operationId, token);
            return;
        }
        auto result = internal::buildSingleSweepFrame(
            pending.work, source_, token, [&] {
                return cancellationRequested(pending.operationId, token);
            });
        if (std::holds_alternative<internal::SweepPipelineCanceled>(result)) {
            (void)finishCancellation(pending.operationId, token);
            return;
        }
        if (const auto* error = std::get_if<OperationFailure>(&result)) {
            fail(pending.operationId, *error);
            return;
        }
        // Publish is the commit point. Completion comes afterward so every
        // terminal observer can retrieve the immutable frame.
        const auto published =
            frames_.publish(std::move(std::get<TraceDisplayFrame>(result)));
        if (!published.hasValue()) {
            fail(pending.operationId,
                 OperationFailure{
                     .code =
                         SingleSweepFailureCode::TraceDisplayPublishFailed,
                     .cause = published.error()});
            return;
        }
        (void)operations_.complete(pending.operationId, OperationSucceeded{});
    }

    bool cancellationRequested(
        OperationId operationId,
        std::stop_token token) {
        if (token.stop_requested()) {
            (void)operations_.requestCancel(operationId);
        }
        const auto current = operations_.snapshot(operationId);
        const auto* snapshot = std::get_if<OperationSnapshot>(&current);
        return snapshot != nullptr && std::holds_alternative<
                                          OperationCancelRequested>(
                                          snapshot->state);
    }

    bool finishCancellation(
        OperationId operationId,
        std::stop_token token) {
        if (!cancellationRequested(operationId, token)) {
            return false;
        }
        (void)operations_.complete(operationId, OperationCanceled{});
        return true;
    }

    void cancel(OperationId operationId) {
        (void)operations_.requestCancel(operationId);
        (void)operations_.complete(operationId, OperationCanceled{});
    }

    void fail(OperationId operationId, OperationFailure failure) {
        (void)operations_.complete(
            operationId,
            OperationFailed{std::move(failure)});
    }

    const std::size_t capacity_;
    RawSweepSource source_;
    OperationManager& operations_;
    TraceDisplayFrameRepository& frames_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Pending> queue_;
    bool accepting_{true};
    std::optional<OperationId> running_;
    std::jthread worker_;
};

SingleSweepExecutor::SingleSweepExecutor(
    std::size_t queueCapacity,
    RawSweepSource source,
    OperationManager& operations,
    TraceDisplayFrameRepository& frames) {
    if (queueCapacity == 0 || !source) {
        throw std::invalid_argument{
            "single sweep executor requires capacity and source"};
    }
    impl_ = std::make_unique<Impl>(
        queueCapacity, std::move(source), operations, frames);
}

SingleSweepExecutor::~SingleSweepExecutor() {
    stop();
}

SingleSweepSubmitResult SingleSweepExecutor::submit(
    SingleSweepWorkItem work) {
    return impl_->submit(std::move(work));
}

void SingleSweepExecutor::stop() {
    impl_->stop();
}

}  // namespace vna::application
