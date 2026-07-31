#pragma once

#include <cstddef>
#include <mutex>
#include <variant>
#include <vector>

#include <vna/application/single_sweep_executor.hpp>

namespace vna::application::internal {

struct SweepTraceRetired {};
using SweepTracePublishResult = std::variant<
    std::monostate,
    OperationFailure,
    SweepTraceRetired>;

// Tracks the bounded set of admitted work without allocating during Trace
// retirement. It serializes retirement with the publish commit point, while
// Operation completion remains the worker's sole responsibility.
class SingleSweepTraceRegistry {
public:
    SingleSweepTraceRegistry(
        std::size_t queueCapacity,
        OperationManager& operations,
        TraceDisplayFrameRepository& frames);

    void registerWork(
        OperationId operationId,
        display_model::TraceId traceId) noexcept;
    [[nodiscard]] SweepTracePublishResult publish(
        OperationId operationId,
        const TraceDisplayPublisher& publisher,
        TraceDisplayFrame frame) noexcept;
    void finish(OperationId operationId) noexcept;
    void discardTrace(display_model::TraceId traceId) noexcept;

private:
    struct Entry {
        OperationId operationId{0};
        display_model::TraceId traceId{0};
        bool active{false};
        bool retired{false};
        bool publishing{false};
        bool discardAfterFinish{false};
    };

    [[nodiscard]] Entry& entryFor(OperationId operationId) noexcept;
    void invokeDiscard(display_model::TraceId traceId) noexcept;

    OperationManager& operations_;
    TraceDisplayFrameRepository& frames_;
    std::mutex mutex_;
    std::vector<Entry> entries_;
};

}  // namespace vna::application::internal
