#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <variant>

#include <vna/application/single_sweep_executor.hpp>

namespace vna::application {

using SingleSweepSubmit =
    std::function<SingleSweepSubmitResult(SingleSweepWorkItem)>;

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
    // submit must remain callable for this handler's lifetime. It is invoked
    // under the handler lock, must return promptly, and must not re-enter it.
    explicit SingleSweepCommandHandler(SingleSweepSubmit submit);

    // Thread-safe. Only an accepted executor result commits ID candidates.
    [[nodiscard]] SingleSweepCommandResult submit(CapturedSingleSweep capture);

private:
    std::mutex mutex_;
    SingleSweepSubmit submit_;
    std::uint64_t nextFrameId_{1};
    std::uint64_t nextSweepId_{1};
    std::uint64_t nextFrequencyAxisId_{1};
    std::unordered_map<std::uint64_t, std::uint64_t> committedSequenceByChannel_;
};

}  // namespace vna::application
