#include <vna/application/command_bus.hpp>

#include "control_authority_internal.hpp"

#include <algorithm>
#include <exception>
#include <type_traits>
#include <utility>

#include <vna/application/trace_publication_catalog.hpp>

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

}  // namespace

CommandResult CommandBus::commitTraceConfiguration(
    domain::Instrument candidateInstrument,
    display_model::DisplayWorkspace candidateDisplay,
    CommandValue value) {
    const auto nextRevision = stateRevision_ + 1;
    const StateSnapshot candidate{
        .stateRevision = nextRevision,
        .control = controlAuthority_->snapshot(),
        .instrument = candidateInstrument.snapshot(),
        .display = candidateDisplay.snapshot(),
    };
    auto prepared = tracePublicationCatalog_.prepare(candidate, nextRevision);
    if (std::holds_alternative<TracePublicationCatalogError>(prepared)) {
        return applicationError(
            ApplicationErrorCode::TraceConfigurationRejected);
    }
    CommandResult result{
        .stateRevision = nextRevision,
        .outcome = CommandSuccess{.value = std::move(value)},
    };
    auto committed = tracePublicationCatalog_.commit(
        std::get<PreparedTracePublicationPlan>(std::move(prepared)));
    if (std::holds_alternative<TracePublicationCatalogError>(committed)) {
        // CommandBus is the sole plan committer and serializes every candidate.
        // A stale/repository error here means that invariant is broken; do not
        // publish a success or attempt a partial rollback after generation moved.
        std::terminate();
    }
    static_assert(std::is_nothrow_move_assignable_v<domain::Instrument>);
    static_assert(std::is_nothrow_move_assignable_v<
        display_model::DisplayWorkspace>);
    instrument_ = std::move(candidateInstrument);
    displayWorkspace_ = std::move(candidateDisplay);
    stateRevision_ = nextRevision;
    return result;
}

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
    return commitTraceConfiguration(
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
    return commitTraceConfiguration(
        std::move(candidateInstrument),
        std::move(candidateDisplay),
        CommandValue{trace.value()});
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
    return commitTraceConfiguration(
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
    return commitTraceConfiguration(
        std::move(candidateInstrument),
        std::move(candidateDisplay),
        CommandValue{std::monostate{}});
}

}  // namespace vna::application
