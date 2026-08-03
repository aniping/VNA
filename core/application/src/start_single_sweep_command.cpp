#include <vna/application/command_bus.hpp>
#include <vna/application/sweep_runtime.hpp>

#include <algorithm>
#include <utility>

namespace vna::application {

CommandResult CommandBus::execute(
    const StartSingleSweepCommand& command,
    const CommandEnvelope& envelope) {
    const auto instrument = instrument_.snapshot();
    const auto channel = std::find_if(
        instrument.channels.cbegin(),
        instrument.channels.cend(),
        [&command](const domain::ChannelSnapshot& candidate) {
            return candidate.id == command.channelId;
        });
    if (channel == instrument.channels.cend()) {
        return domainError(domain::DomainError{
            .code = domain::DomainErrorCode::ChannelNotFound});
    }
    // Runtime already owns the full immutable publication plan. Admission
    // carries correlation only; it must not rebuild a legacy one-Trace plan.
    const auto submitted = sweepRuntime_.requestRestart({
        envelope.commandId, envelope.sessionId, stateRevision_});
    if (std::holds_alternative<SweepRuntimeRequestError>(submitted)) {
        return applicationError(ApplicationErrorCode::ResourceBusy);
    }
    const auto operationId = std::get<OperationId>(submitted);
    return CommandResult{
        .stateRevision = stateRevision_,
        .outcome = CommandSuccess{.value = CommandValue{operationId}},
    };
}

}  // namespace vna::application
