#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <variant>

#include <vna/application/single_sweep_executor.hpp>

namespace vna::application {

// CommandBus supplies one immutable state capture. The handler exclusively
// assigns acquisition correlation, so rejected admission cannot create gaps.
struct CapturedSingleSweep {
    CommandId commandId;
    SessionId sessionId;
    std::uint64_t stateRevision;
    domain::ChannelSnapshot channel;
    domain::MeasurementSnapshot measurement;
    display_model::TraceSnapshot trace;
};

using SingleSweepCommandResult =
    std::variant<OperationId, SingleSweepSubmitError>;

class SingleSweepCommandHandler {
public:
    // The execution owner must outlive this handler. Admission and retirement
    // are both quick, non-reentrant calls made under application transaction
    // locks; retirement is noexcept and never delivers completion callbacks.
    explicit SingleSweepCommandHandler(
        SingleSweepExecution& execution) noexcept;

    // Thread-safe. Only an accepted executor result commits ID candidates.
    [[nodiscard]] SingleSweepCommandResult submit(CapturedSingleSweep capture);

    // Trace deletion is already committed before this cleanup begins. Adapter
    // failure is contained so stale frame cleanup can never restore the Trace.
    void discard(display_model::TraceId traceId) noexcept;

private:
    std::mutex mutex_;
    SingleSweepExecution& execution_;
    std::uint64_t nextFrameId_{1};
    std::uint64_t nextSweepId_{1};
    std::uint64_t nextFrequencyAxisId_{1};
    std::unordered_map<std::uint64_t, std::uint64_t> committedSequenceByChannel_;
};

}  // namespace vna::application
