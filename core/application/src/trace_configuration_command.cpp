#include <vna/application/command_bus.hpp>

#include "control_authority_internal.hpp"

#include <algorithm>
#include <utility>

namespace vna::application {
namespace {

const display_model::TraceSnapshot* findTrace(
    const display_model::DisplayWorkspaceSnapshot& snapshot,
    display_model::TraceId traceId) {
    const auto found = std::find_if(
        snapshot.traces.cbegin(), snapshot.traces.cend(),
        [traceId](const auto& trace) { return trace.id == traceId; });
    return found == snapshot.traces.cend() ? nullptr : &*found;
}

const domain::MeasurementSnapshot* findMeasurement(
    const domain::InstrumentSnapshot& snapshot,
    domain::MeasurementId measurementId) {
    const auto found = std::find_if(
        snapshot.measurements.cbegin(), snapshot.measurements.cend(),
        [measurementId](const auto& item) { return item.id == measurementId; });
    return found == snapshot.measurements.cend() ? nullptr : &*found;
}

bool supports(domain::MeasurementType type) {
    switch (type) {
        case domain::MeasurementType::S11:
        case domain::MeasurementType::S21:
        case domain::MeasurementType::S12:
        case domain::MeasurementType::S22:
            return true;
    }
    return false;
}

const domain::MeasurementSnapshot* findTypeInChannel(
    const domain::InstrumentSnapshot& snapshot,
    domain::ChannelId channelId,
    domain::MeasurementType type) {
    const auto found = std::find_if(
        snapshot.measurements.cbegin(), snapshot.measurements.cend(),
        [channelId, type](const auto& item) {
            return item.channelId == channelId && item.type == type;
        });
    return found == snapshot.measurements.cend() ? nullptr : &*found;
}

}  // namespace

CommandResult CommandBus::succeededWithoutRevision(CommandValue value) const {
    return {
        .stateRevision = stateRevision_,
        .outcome = CommandSuccess{.value = std::move(value)},
    };
}

CommandResult CommandBus::execute(const CreateTraceCommand& command) {
    if (!instrument_.containsMeasurement(command.measurementId)) {
        return domainError(domain::DomainError{
            .code = domain::DomainErrorCode::MeasurementNotFound});
    }
    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto trace = candidateDisplay.createTrace(
        command.windowId, command.measurementId, command.format);
    if (!trace.hasValue()) {
        return displayError(trace.error());
    }
    return commitConfiguration(
        std::move(candidateInstrument),
        std::move(candidateDisplay),
        CommandValue{trace.value()});
}

CommandResult CommandBus::execute(const UpdateTraceFormatCommand& command) {
    const auto snapshot = displayWorkspace_.snapshot();
    const auto* current = findTrace(snapshot, command.traceId);
    if (current == nullptr) {
        return displayError(display_model::DisplayError{
            .code = display_model::DisplayErrorCode::TraceNotFound});
    }
    if (current->format == command.format) {
        return succeededWithoutRevision(CommandValue{command.traceId});
    }
    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto trace =
        candidateDisplay.updateTraceFormat(command.traceId, command.format);
    return commitConfiguration(
        std::move(candidateInstrument),
        std::move(candidateDisplay),
        CommandValue{trace.value()});
}

CommandResult CommandBus::execute(
    const SetTraceMeasurementTypeCommand& command) {
    const auto display = displayWorkspace_.snapshot();
    const auto* trace = findTrace(display, command.traceId);
    if (trace == nullptr) {
        return displayError(display_model::DisplayError{
            .code = display_model::DisplayErrorCode::TraceNotFound});
    }
    const auto instrument = instrument_.snapshot();
    const auto* current = findMeasurement(instrument, trace->measurementId);
    if (current == nullptr || !supports(command.measurementType)) {
        return applicationError(
            ApplicationErrorCode::TraceConfigurationRejected);
    }
    if (current->type == command.measurementType) {
        return succeededWithoutRevision(CommandValue{command.traceId});
    }

    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto* existing = findTypeInChannel(
        instrument, current->channelId, command.measurementType);
    auto measurementId = domain::MeasurementId{0};
    if (existing != nullptr) {
        measurementId = existing->id;
    } else {
        const auto created = candidateInstrument.createMeasurement(
            current->channelId, command.measurementType);
        if (!created.hasValue()) {
            return domainError(created.error());
        }
        measurementId = created.value();
    }
    const auto rebound = candidateDisplay.updateTraceMeasurement(
        command.traceId, measurementId);
    if (!rebound.hasValue()) {
        return displayError(rebound.error());
    }
    return commitConfiguration(
        std::move(candidateInstrument),
        std::move(candidateDisplay),
        CommandValue{command.traceId});
}

CommandResult CommandBus::execute(
    const UpdateTraceScalePerDivisionCommand& command) {
    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto trace = candidateDisplay.updateTraceScalePerDivision(
        command.traceId, command.scalePerDivision);
    if (!trace.hasValue()) {
        return displayError(trace.error());
    }
    return commitConfiguration(
        std::move(candidateInstrument),
        std::move(candidateDisplay),
        CommandValue{trace.value()});
}

CommandResult CommandBus::execute(const RemoveTraceCommand& command) {
    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto trace = candidateDisplay.removeTrace(command.traceId);
    if (!trace.hasValue()) {
        return displayError(trace.error());
    }
    return commitConfiguration(
        std::move(candidateInstrument),
        std::move(candidateDisplay),
        CommandValue{std::monostate{}});
}

}  // namespace vna::application
