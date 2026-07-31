#include <vna/application/command_bus.hpp>

namespace vna::application {

CommandErrorCode commandErrorCode(const CommandError& error) noexcept {
    const auto* domainError = std::get_if<domain::DomainError>(&error);
    if (domainError != nullptr) {
        switch (domainError->code) {
            case domain::DomainErrorCode::InvalidSweepSettings:
                return CommandErrorCode::InvalidSweepSettings;
            case domain::DomainErrorCode::ChannelNotFound:
                return CommandErrorCode::ChannelNotFound;
            case domain::DomainErrorCode::MeasurementNotFound:
                return CommandErrorCode::MeasurementNotFound;
        }
    }
    const auto* displayError =
        std::get_if<display_model::DisplayError>(&error);
    if (displayError != nullptr) {
        switch (displayError->code) {
            case display_model::DisplayErrorCode::WindowNotFound:
                return CommandErrorCode::WindowNotFound;
            case display_model::DisplayErrorCode::TraceNotFound:
                return CommandErrorCode::TraceNotFound;
            case display_model::DisplayErrorCode::InvalidScalePerDivision:
                return CommandErrorCode::InvalidScalePerDivision;
            case display_model::DisplayErrorCode::ScaleNotSupportedForFormat:
                return CommandErrorCode::ScaleNotSupportedForFormat;
        }
    }
    return std::get<ApplicationError>(error).code;
}

CommandBus::CommandBus(InstrumentId instrumentId)
    : instrumentId_(std::move(instrumentId)) {}

CommandResult CommandBus::dispatch(const CommandEnvelope& command) {
    const std::scoped_lock lock{mutex_};
    if (command.instrumentId != instrumentId_) {
        return applicationError(CommandErrorCode::WrongInstrument);
    }

    if (command.expectedStateRevision.has_value() &&
        command.expectedStateRevision.value() != stateRevision_) {
        return applicationError(CommandErrorCode::StateRevisionConflict);
    }

    return std::visit(
        [this](const auto& payload) { return execute(payload); },
        command.payload);
}

CommandResult CommandBus::execute(const CreateChannelCommand& command) {
    const auto channel = instrument_.createChannel(command.sweep);
    if (!channel.hasValue()) {
        return domainError(channel.error());
    }
    return succeeded(CommandValue{channel.value()});
}

CommandResult CommandBus::execute(const UpdateChannelSweepCommand& command) {
    const auto channel =
        instrument_.updateChannelSweep(command.channelId, command.sweep);
    if (!channel.hasValue()) {
        return domainError(channel.error());
    }
    return succeeded(CommandValue{channel.value()});
}

CommandResult CommandBus::execute(const CreateMeasurementCommand& command) {
    const auto measurement =
        instrument_.createMeasurement(command.channelId, command.type);
    if (!measurement.hasValue()) {
        return domainError(measurement.error());
    }
    return succeeded(CommandValue{measurement.value()});
}

CommandResult CommandBus::execute(const CreateWindowCommand&) {
    return succeeded(CommandValue{displayWorkspace_.createWindow()});
}

CommandResult CommandBus::execute(const CreateTraceCommand& command) {
    if (!instrument_.containsMeasurement(command.measurementId)) {
        return domainError(domain::DomainError{
            .code = domain::DomainErrorCode::MeasurementNotFound});
    }
    const auto trace = displayWorkspace_.createTrace(
        command.windowId,
        command.measurementId,
        command.format);
    if (!trace.hasValue()) {
        return displayError(trace.error());
    }
    return succeeded(CommandValue{trace.value()});
}

CommandResult CommandBus::execute(const UpdateTraceFormatCommand& command) {
    const auto trace =
        displayWorkspace_.updateTraceFormat(command.traceId, command.format);
    if (!trace.hasValue()) {
        return displayError(trace.error());
    }
    return succeeded(CommandValue{trace.value()});
}

CommandResult CommandBus::execute(const RemoveTraceCommand& command) {
    const auto trace = displayWorkspace_.removeTrace(command.traceId);
    if (!trace.hasValue()) {
        return displayError(trace.error());
    }
    return succeeded(CommandValue{std::monostate{}});
}

CommandResult CommandBus::succeeded(CommandValue value) {
    ++stateRevision_;
    return CommandResult{
        .stateRevision = stateRevision_,
        .outcome = CommandSuccess{.value = std::move(value)},
    };
}

CommandResult CommandBus::domainError(domain::DomainError error) const {
    return CommandResult{
        .stateRevision = stateRevision_,
        .outcome = CommandError{error},
    };
}

CommandResult CommandBus::displayError(
    display_model::DisplayError error) const {
    return CommandResult{
        .stateRevision = stateRevision_,
        .outcome = CommandError{error},
    };
}

CommandResult CommandBus::applicationError(CommandErrorCode code) const {
    return CommandResult{
        .stateRevision = stateRevision_,
        .outcome = CommandError{ApplicationError{.code = code}},
    };
}

StateSnapshot CommandBus::snapshot() const {
    const std::scoped_lock lock{mutex_};
    return StateSnapshot{
        .stateRevision = stateRevision_,
        .instrument = instrument_.snapshot(),
        .display = displayWorkspace_.snapshot(),
    };
}

}  // namespace vna::application
