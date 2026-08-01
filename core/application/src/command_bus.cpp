#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>

#include "control_authority_internal.hpp"
#include "command_idempotency_internal.hpp"

#include <exception>
#include <type_traits>

#include <vna/application/single_sweep_command_handler.hpp>

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
    return commandErrorCode(std::get<ApplicationError>(error));
}

CommandErrorCode commandErrorCode(const ApplicationError& error) noexcept {
    switch (error.code) {
        case ApplicationErrorCode::CommandIdReuse:
            return CommandErrorCode::CommandIdReuse;
        case ApplicationErrorCode::ControlDenied:
            return CommandErrorCode::ControlDenied;
        case ApplicationErrorCode::ResourceBusy:
            return CommandErrorCode::ResourceBusy;
        case ApplicationErrorCode::StateRevisionConflict:
            return CommandErrorCode::StateRevisionConflict;
        case ApplicationErrorCode::UnsupportedSweepConfiguration:
            return CommandErrorCode::UnsupportedSweepConfiguration;
        case ApplicationErrorCode::WrongInstrument:
            return CommandErrorCode::WrongInstrument;
    }
    std::terminate();
}

CommandBus::CommandBus(
    InstrumentId instrumentId,
    SingleSweepCommandHandler& singleSweepHandler,
    std::size_t idempotencyCapacity)
    : CommandBus(std::move(instrumentId),
          singleSweepHandler,
          CommandBusInitialState{},
          idempotencyCapacity) {}

CommandBus::CommandBus(
    InstrumentId instrumentId,
    SingleSweepCommandHandler& singleSweepHandler,
    CommandBusInitialState initialState,
    std::size_t idempotencyCapacity)
    : instrumentId_(std::move(instrumentId)),
      singleSweepHandler_(singleSweepHandler),
      instrument_(std::move(initialState.instrument)),
      displayWorkspace_(std::move(initialState.displayWorkspace)),
      idempotency_(
          std::make_unique<IdempotencyStore>(idempotencyCapacity)),
      controlAuthority_(std::make_unique<ControlAuthority>()) {}

CommandBus::~CommandBus() = default;

CommandResult CommandBus::dispatch(const CommandEnvelope& command) {
    const std::scoped_lock lock{mutex_};
    if (command.instrumentId != instrumentId_) {
        return applicationError(ApplicationErrorCode::WrongInstrument);
    }

    const auto lookup = idempotency_->lookup(command);
    if (lookup.replay != nullptr) {
        return *lookup.replay;
    }
    if (lookup.keyFound) {
        return applicationError(ApplicationErrorCode::CommandIdReuse);
    }

    if (!controlAuthority_->authorizes(command.origin, command.sessionId)) {
        return applicationError(ApplicationErrorCode::ControlDenied);
    }

    // Preparing the immutable cache key is the final throwing boundary before
    // any command side effect or asynchronous admission can become visible.
    auto prepared = idempotency_->prepare(command);
    if (command.expectedStateRevision.has_value() &&
        command.expectedStateRevision.value() != stateRevision_) {
        auto result =
            applicationError(ApplicationErrorCode::StateRevisionConflict);
        idempotency_->commit(std::move(prepared), result);
        return result;
    }

    const auto result = std::visit(
        [this, &command](const auto& payload) {
            if constexpr (std::is_same_v<
                              std::decay_t<decltype(payload)>,
                              StartSingleSweepCommand>) {
                return execute(payload, command);
            } else {
                return execute(payload);
            }
        },
        command.payload);
    if (idempotency_->isCacheable(result)) {
        idempotency_->commit(std::move(prepared), result);
    }
    return result;
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

CommandResult CommandBus::execute(
    const UpdateTraceScalePerDivisionCommand& command) {
    const auto trace = displayWorkspace_.updateTraceScalePerDivision(
        command.traceId,
        command.scalePerDivision);
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
    singleSweepHandler_.discard(command.traceId);
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

CommandResult CommandBus::applicationError(ApplicationErrorCode code) const {
    return CommandResult{
        .stateRevision = stateRevision_,
        .outcome = CommandError{ApplicationError{.code = code}},
    };
}

StateSnapshot CommandBus::snapshot() const {
    const std::scoped_lock lock{mutex_};
    return StateSnapshot{
        .stateRevision = stateRevision_,
        .control = controlAuthority_->snapshot(),
        .instrument = instrument_.snapshot(),
        .display = displayWorkspace_.snapshot(),
    };
}

CommandBusStats CommandBus::stats() const {
    const std::scoped_lock lock{mutex_};
    return idempotency_->stats();
}

}  // namespace vna::application
