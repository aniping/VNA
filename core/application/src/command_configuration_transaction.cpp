#include <vna/application/command_bus.hpp>

#include "control_authority_internal.hpp"

#include <type_traits>
#include <utility>

#include <vna/application/sweep_runtime.hpp>

namespace vna::application {
namespace {

ApplicationErrorCode mapConfigurationError(
    SweepRuntimeConfigurationErrorCode code) noexcept {
    switch (code) {
        case SweepRuntimeConfigurationErrorCode::UnsupportedSweepConfiguration:
            return ApplicationErrorCode::UnsupportedSweepConfiguration;
        case SweepRuntimeConfigurationErrorCode::TraceConfigurationRejected:
            return ApplicationErrorCode::TraceConfigurationRejected;
        case SweepRuntimeConfigurationErrorCode::Stopped:
        case SweepRuntimeConfigurationErrorCode::Retired:
        case SweepRuntimeConfigurationErrorCode::Failed:
            return ApplicationErrorCode::ResourceBusy;
    }
    std::terminate();
}

}  // namespace

CommandResult CommandBus::commitConfiguration(
    domain::Instrument candidateInstrument,
    display_model::DisplayWorkspace candidateDisplay,
    CommandValue value) {
    const auto nextRevision = stateRevision_ + 1;
    const StateSnapshot candidate{
        nextRevision, controlAuthority_->snapshot(),
        candidateInstrument.snapshot(), candidateDisplay.snapshot()};
    auto prepared = sweepRuntime_.prepareConfiguration(candidate);
    if (const auto* error =
            std::get_if<SweepRuntimeConfigurationError>(&prepared)) {
        return applicationError(mapConfigurationError(error->code));
    }
    CommandResult result{
        nextRevision, CommandSuccess{.value = std::move(value)}};
    static_assert(std::is_nothrow_move_assignable_v<domain::Instrument>);
    static_assert(std::is_nothrow_move_assignable_v<
        display_model::DisplayWorkspace>);
    instrument_ = std::move(candidateInstrument);
    displayWorkspace_ = std::move(candidateDisplay);
    stateRevision_ = nextRevision;
    sweepRuntime_.commitConfiguration(std::get<
        PreparedSweepRuntimeConfiguration>(std::move(prepared)));
    return result;
}

}  // namespace vna::application
