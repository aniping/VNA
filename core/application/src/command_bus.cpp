#include <vna/application/command_bus.hpp>

namespace vna::application {

CommandBus::CommandBus(InstrumentId instrumentId)
    : instrumentId_(std::move(instrumentId)) {}

CommandResult CommandBus::dispatch(const CommandEnvelope& command) {
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

    if (const auto* payload =
            std::get_if<CreateChannelCommand>(&command.payload)) {
        const auto channel = instrument_.createChannel(payload->sweep);
        if (!channel.hasValue()) {
            return validationError();
        }
        return succeeded(CommandValue{channel.value()});
    }

    if (const auto* payload =
            std::get_if<CreateMeasurementCommand>(&command.payload)) {
        const auto measurement =
            instrument_.createMeasurement(payload->channelId, payload->type);
        if (!measurement.hasValue()) {
            return validationError();
        }
        return succeeded(CommandValue{measurement.value()});
    }

    if (std::holds_alternative<CreateWindowCommand>(command.payload)) {
        return succeeded(CommandValue{instrument_.createWindow()});
    }

    const auto& payload = std::get<CreateTraceCommand>(command.payload);
    const auto trace = instrument_.createTrace(
        payload.windowId,
        payload.measurementId,
        payload.format);
    if (!trace.hasValue()) {
        return validationError();
    }
    return succeeded(CommandValue{trace.value()});
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
    return StateSnapshot{
        .stateRevision = stateRevision_,
        .instrument = instrument_.snapshot(),
    };
}

}  // namespace vna::application
