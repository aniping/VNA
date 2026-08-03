#include <vna/application/single_sweep_executor.hpp>

#include <condition_variable>
#include <deque>
#include <optional>
#include <stdexcept>
#include <vna/compat/joining_thread.hpp>
#include <utility>

#include "single_sweep_pipeline_internal.hpp"
#include "single_sweep_operation_internal.hpp"
#include "single_sweep_trace_registry_internal.hpp"

namespace vna::application {

class SingleSweepExecutor::Impl {
public:
    Impl(
        std::size_t capacity,
        RawSweepSource source,
        OperationManager& operations,
        TraceDisplayPublisher publish,
        TraceDisplayFrameRepository& frames)
        : capacity_(capacity),
          source_(std::move(source)),
          operations_(operations),
          publish_(std::move(publish)),
          traceRegistry_(capacity, operations, frames),
          worker_([this](vna::compat::StopToken token) { workerLoop(token); }) {}

    SingleSweepSubmitResult submit(SingleSweepWorkItem work) {
        std::unique_lock lock{mutex_};
        if (!accepting_) {
            return SingleSweepSubmitError{
                .code = SingleSweepSubmitErrorCode::Stopped};
        }
        if (queue_.size() >= capacity_) {
            return SingleSweepSubmitError{
                .code = SingleSweepSubmitErrorCode::QueueFull};
        }
        // Queue allocation happens before Operation creation, but the worker
        // cannot observe Pending before unlock/notify. create either rolls its
        // own state back or returns a complete Snapshot; after that boundary,
        // assigning and returning OperationId are both non-throwing.
        queue_.push_back(Pending{.work = std::move(work)});
        try {
            const auto& queued = queue_.back().work;
            auto operation = operations_.create(OperationSubmission{
                queued.commandId,
                queued.sessionId,
                queued.frameContext.stateRevision});
            queue_.back().operationId = operation.id;
            traceRegistry_.registerWork(operation.id, queued.traceId);
            condition_.notify_one();
            return operation.id;
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
            worker_.requestStop();
            abandoned.swap(queue_);
            running = running_;
        }
        // Operation completion can invoke external fence callbacks, so no
        // executor lock crosses cancellation or join boundaries.
        condition_.notify_all();
        for (const auto& pending : abandoned) {
            internal::cancelSingleSweepOperation(
                operations_, pending.operationId);
        }
        if (running) {
            (void)operations_.requestCancel(*running);
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void discardTrace(display_model::TraceId traceId) noexcept {
        traceRegistry_.discardTrace(traceId);
    }
    void invalidateTraceFrame(display_model::TraceId traceId) noexcept {
        traceRegistry_.invalidateTraceFrame(traceId);
    }

private:
    struct Pending {
        SingleSweepWorkItem work;
        OperationId operationId{0};
    };

    void workerLoop(vna::compat::StopToken token) {
        while (true) {
            std::optional<Pending> pending;
            {
                std::unique_lock lock{mutex_};
                condition_.wait(lock, [&] {
                    return token.stopRequested() || !queue_.empty();
                });
                if (queue_.empty()) {
                    return;
                }
                pending.emplace(std::move(queue_.front()));
                queue_.pop_front();
                running_ = pending->operationId;
            }
            runGuarded(std::move(*pending), token);
            traceRegistry_.finish(pending->operationId);
            std::lock_guard lock{mutex_};
            running_.reset();
        }
    }

    void runGuarded(Pending pending, vna::compat::StopToken token) {
        const auto operationId = pending.operationId;
        try {
            run(std::move(pending), token);
        } catch (...) {
            // Allocation and third-party exceptions outside the explicitly
            // typed stages must not terminate the sole worker or strand the
            // accepted Operation in Running.
            (void)operations_.markRunning(operationId);
            internal::failSingleSweepOperation(
                operations_, operationId,
                OperationFailure{
                    .code = SingleSweepFailureCode::UnexpectedFailure,
                    .cause = std::current_exception()});
        }
    }

    void run(Pending pending, vna::compat::StopToken token) {
        if (internal::finishSingleSweepCancellation(
                operations_, pending.operationId, token)) {
            return;
        }
        const auto running = operations_.markRunning(pending.operationId);
        if (!std::holds_alternative<OperationSnapshot>(running)) {
            (void)internal::finishSingleSweepCancellation(
                operations_, pending.operationId, token);
            return;
        }
        auto result = internal::buildSingleSweepFrame(
            pending.work, source_, token, [&] {
                return internal::singleSweepCancellationRequested(
                    operations_, pending.operationId, token);
            });
        if (std::holds_alternative<internal::SweepPipelineCanceled>(result)) {
            (void)internal::finishSingleSweepCancellation(
                operations_, pending.operationId, token);
            return;
        }
        if (const auto* error = std::get_if<OperationFailure>(&result)) {
            internal::failSingleSweepOperation(
                operations_, pending.operationId, *error);
            return;
        }
        const auto publication = traceRegistry_.publish(
            pending.operationId,
            publish_,
            std::move(std::get<TraceDisplayFrame>(result)));
        if (std::holds_alternative<internal::SweepTraceRetired>(publication)) {
            (void)internal::finishSingleSweepCancellation(
                operations_, pending.operationId, token);
            return;
        }
        if (const auto* failure = std::get_if<OperationFailure>(&publication)) {
            internal::failSingleSweepOperation(
                operations_, pending.operationId, *failure);
            return;
        }
        // No-throw Operation values leave no allocation window after the frame
        // has become visible.
        (void)operations_.complete(
            pending.operationId,
            OperationSucceeded{pending.work.frameContext.frameId});
    }

    const std::size_t capacity_;
    RawSweepSource source_;
    OperationManager& operations_;
    TraceDisplayPublisher publish_;
    internal::SingleSweepTraceRegistry traceRegistry_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Pending> queue_;
    bool accepting_{true};
    std::optional<OperationId> running_;
    vna::compat::JoiningThread worker_;
};

SingleSweepExecutor::SingleSweepExecutor(
    std::size_t queueCapacity,
    RawSweepSource source,
    OperationManager& operations,
    TraceDisplayFrameRepository& frames,
    TraceDisplayPublisher publish) {
    if (queueCapacity == 0 || !source || !publish) {
        throw std::invalid_argument{
            "single sweep executor requires capacity and lifecycle ports"};
    }
    impl_ = std::make_unique<Impl>(
        queueCapacity, std::move(source), operations,
        std::move(publish), frames);
}

SingleSweepExecutor::~SingleSweepExecutor() {
    stop();
}

SingleSweepSubmitResult SingleSweepExecutor::submit(
    SingleSweepWorkItem work) {
    return impl_->submit(std::move(work));
}

void SingleSweepExecutor::invalidateTraceFrame(
    display_model::TraceId traceId) noexcept {
    impl_->invalidateTraceFrame(traceId);
}

void SingleSweepExecutor::discardTrace(
    display_model::TraceId traceId) noexcept {
    impl_->discardTrace(traceId);
}

void SingleSweepExecutor::stop() {
    impl_->stop();
}

}  // namespace vna::application
