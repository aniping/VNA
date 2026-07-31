#include <vna/application/command_bus.hpp>

namespace vna::application {

CommandBus::CommandBus(InstrumentId instrumentId)
    : instrumentId_(std::move(instrumentId)) {}

CommandResult CommandBus::dispatch(const CommandEnvelope& command) {
    const std::scoped_lock lock{mutex_};
    if (command.instrumentId != instrumentId_) {
        return CommandResult{
            .status = CommandStatus::WrongInstrument,
            .stateRevision = stateRevision_,
        };
    }

    if (command.expectedStateRevision.has_value() &&
        command.expectedStateRevision.value() != stateRevision_) {
        return CommandResult{
            .status = CommandStatus::Conflict,
            .stateRevision = stateRevision_,
        };
    }

    return std::visit(
        [this](const auto& payload) { return execute(payload); },
        command.payload);
}

CommandResult CommandBus::execute(const CreateChannelCommand& command) {
    const auto channel = instrument_.createChannel(command.sweep);
    if (!channel.hasValue()) {
        return validationError();
    }
    return succeeded(CommandValue{channel.value()});
}

CommandResult CommandBus::execute(const UpdateChannelSweepCommand& command) {
    const auto channel =
        instrument_.updateChannelSweep(command.channelId, command.sweep);
    if (!channel.hasValue()) {
        return validationError();
    }
    return succeeded(CommandValue{channel.value()});
}

CommandResult CommandBus::execute(const CreateMeasurementCommand& command) {
    const auto measurement =
        instrument_.createMeasurement(command.channelId, command.type);
    if (!measurement.hasValue()) {
        return validationError();
    }
    return succeeded(CommandValue{measurement.value()});
}

CommandResult CommandBus::execute(const CreateWindowCommand&) {
    return succeeded(CommandValue{instrument_.createWindow()});
}

CommandResult CommandBus::execute(const CreateTraceCommand& command) {
    const auto trace = instrument_.createTrace(
        command.windowId,
        command.measurementId,
        command.format);
    if (!trace.hasValue()) {
        return validationError();
    }
    return succeeded(CommandValue{trace.value()});
}

CommandResult CommandBus::execute(const RemoveTraceCommand& command) {
    if (!instrument_.removeTrace(command.traceId)) {
        return validationError();
    }
    return succeeded(CommandValue{std::monostate{}});
}

CommandResult CommandBus::succeeded(CommandValue value) {
    ++stateRevision_;
    return CommandResult{
        .status = CommandStatus::Succeeded,
        .stateRevision = stateRevision_,
        .value = std::move(value),
    };
}

CommandResult CommandBus::validationError() const {
    return CommandResult{
        .status = CommandStatus::ValidationError,
        .stateRevision = stateRevision_,
    };
}

StateSnapshot CommandBus::snapshot() const {
    const std::scoped_lock lock{mutex_};
    return StateSnapshot{
        .stateRevision = stateRevision_,
        .instrument = instrument_.snapshot(),
    };
}

}  // namespace vna::application
