#include <vna/application/single_sweep_command_handler.hpp>

#include <utility>

namespace vna::application {

SingleSweepCommandHandler::SingleSweepCommandHandler(
    SingleSweepExecution& execution) noexcept
    : execution_(execution) {}

SingleSweepCommandResult SingleSweepCommandHandler::submit(
    CapturedSingleSweep capture) {
    std::lock_guard lock{mutex_};
    auto& sequence =
        committedSequenceByChannel_[capture.channel.id.value()];
    SingleSweepWorkItem work{
        .commandId = std::move(capture.commandId),
        .sessionId = std::move(capture.sessionId),
        .frameContext = {
            .frameId = frames::FrameId{nextFrameId_},
            .sweepId = frames::SweepId{nextSweepId_},
            .channelId = capture.channel.id,
            .stateRevision = capture.stateRevision,
            .sequenceNumber = sequence + 1,
        },
        .frequencyAxis = {
            .id = frames::FrequencyAxisId{nextFrequencyAxisId_},
            .startFrequencyHz = capture.channel.sweep.startFrequencyHz,
            .stopFrequencyHz = capture.channel.sweep.stopFrequencyHz,
            .points = capture.channel.sweep.points,
        },
        .measurement = capture.measurement,
        .traceId = capture.trace.id,
    };
    auto submitted = execution_.submit(std::move(work));
    if (const auto* accepted = std::get_if<OperationId>(&submitted)) {
        ++nextFrameId_;
        ++nextSweepId_;
        ++nextFrequencyAxisId_;
        ++sequence;
        return *accepted;
    }
    return std::get<SingleSweepSubmitError>(submitted);
}

void SingleSweepCommandHandler::invalidateFrame(
    display_model::TraceId traceId) noexcept {
    execution_.invalidateTraceFrame(traceId);
}

void SingleSweepCommandHandler::discard(
    display_model::TraceId traceId) noexcept {
    execution_.discardTrace(traceId);
}

}  // namespace vna::application
