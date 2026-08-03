#include "single_sweep_trace_registry_internal.hpp"

#include "single_sweep_publisher_internal.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace vna::application::internal {

SingleSweepTraceRegistry::SingleSweepTraceRegistry(
    std::size_t queueCapacity,
    OperationManager& operations,
    TraceDisplayFrameRepository& frames)
    : operations_(operations),
      frames_(frames),
      entries_(queueCapacity + 1) {}

void SingleSweepTraceRegistry::registerWork(
    OperationId operationId,
    display_model::TraceId traceId) noexcept {
    const std::scoped_lock lock{mutex_};
    const auto available = std::find_if(
        entries_.begin(), entries_.end(),
        [](const Entry& entry) { return !entry.active; });
    if (available == entries_.end()) {
        std::terminate();
    }
    *available = Entry{
        .operationId = operationId,
        .traceId = traceId,
        .active = true};
}

SweepTracePublishResult SingleSweepTraceRegistry::publish(
    OperationId operationId,
    const TraceDisplayPublisher& publisher,
    TraceDisplayFrame frame) noexcept {
    const std::scoped_lock lock{mutex_};
    auto& entry = entryFor(operationId);
    if (entry.retired) {
        return SweepTraceRetired{};
    }
    // Holding the lifecycle gate through the repository commit means a Trace
    // deletion can never return before an older publish that already won the
    // race. Completion remains outside this lock, so fence callbacks may reenter.
    entry.publishing = true;
    const auto failure = publishTraceDisplayFrame(
        publisher, std::move(frame));
    if (failure) {
        return *failure;
    }
    return std::monostate{};
}

void SingleSweepTraceRegistry::finish(OperationId operationId) noexcept {
    display_model::TraceId discarded{0};
    {
        const std::scoped_lock lock{mutex_};
        auto& entry = entryFor(operationId);
        if (entry.discardAfterFinish) {
            discarded = entry.traceId;
        }
        entry = Entry{};
    }
    if (discarded.value() != 0) {
        invokeDiscard(discarded);
    }
}

void SingleSweepTraceRegistry::invalidateTraceFrame(
    display_model::TraceId traceId) noexcept {
    // The same gate spans publish, so invalidation cannot return while an
    // older publish still owns the repository commit point.
    const std::scoped_lock lock{mutex_};
    invokeDiscard(traceId);
}

void SingleSweepTraceRegistry::discardTrace(
    display_model::TraceId traceId) noexcept {
    bool discardNow = true;
    {
        const std::scoped_lock lock{mutex_};
        for (auto& entry : entries_) {
            if (!entry.active || entry.traceId != traceId) {
                continue;
            }
            entry.retired = true;
            (void)operations_.requestCancel(entry.operationId);
            if (entry.publishing) {
                entry.discardAfterFinish = true;
                discardNow = false;
            }
        }
    }
    if (discardNow) {
        invokeDiscard(traceId);
    }
}

SingleSweepTraceRegistry::Entry& SingleSweepTraceRegistry::entryFor(
    OperationId operationId) noexcept {
    const auto found = std::find_if(
        entries_.begin(), entries_.end(), [&](const Entry& entry) {
            return entry.active && entry.operationId == operationId;
        });
    if (found == entries_.end()) {
        std::terminate();
    }
    return *found;
}

void SingleSweepTraceRegistry::invokeDiscard(
    display_model::TraceId traceId) noexcept {
    frames_.discard(traceId);
}

}  // namespace vna::application::internal
