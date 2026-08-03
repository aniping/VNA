#include "sweep_runtime_internal.hpp"

#include "sweep_generation_transaction_internal.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <vna/application/command_bus.hpp>

namespace vna::application::internal {
namespace {

SweepRuntimeConfigurationError unavailable(SweepRuntimeState state) {
    const auto code = state == SweepRuntimeState::Stopped
        ? SweepRuntimeConfigurationErrorCode::Stopped
        : state == SweepRuntimeState::Retired
        ? SweepRuntimeConfigurationErrorCode::Retired
        : SweepRuntimeConfigurationErrorCode::Failed;
    return {code};
}

bool sameAcquisitionPlan(
    const acquisition::ContinuousAcquisitionPlan& left,
    const acquisition::ContinuousAcquisitionPlan& right) {
    return left.frequencyAxis.id == right.frequencyAxis.id &&
        left.frequencyAxis.startFrequencyHz ==
            right.frequencyAxis.startFrequencyHz &&
        left.frequencyAxis.stopFrequencyHz ==
            right.frequencyAxis.stopFrequencyHz &&
        left.frequencyAxis.points == right.frequencyAxis.points &&
        left.portCount == right.portCount &&
        left.sourcePorts == right.sourcePorts &&
        left.ifBandwidthHz == right.ifBandwidthHz &&
        left.powerDbm == right.powerDbm &&
        left.minimumSweepPeriod == right.minimumSweepPeriod;
}

}  // namespace

SweepRuntimeConfigurationPrepareResult
SweepRuntimeImpl::prepareConfiguration(const StateSnapshot& candidate) {
    std::unique_lock gate{mutex_};
    if (snapshot_.state != SweepRuntimeState::Running || admissionClosed_) {
        return unavailable(snapshot_.state);
    }
    const auto channelId = plan_.publication->channelId;
    const auto channel = std::find_if(
        candidate.instrument.channels.cbegin(),
        candidate.instrument.channels.cend(),
        [channelId](const auto& item) { return item.id == channelId; });
    if (channel == candidate.instrument.channels.cend() &&
        !candidate.instrument.channels.empty()) {
        return SweepRuntimeConfigurationError{
            SweepRuntimeConfigurationErrorCode::UnsupportedSweepConfiguration};
    }
    auto acquisition = plan_.acquisition;
    if (channel != candidate.instrument.channels.cend()) {
        acquisition.frequencyAxis.startFrequencyHz =
            channel->sweep.startFrequencyHz;
        acquisition.frequencyAxis.stopFrequencyHz =
            channel->sweep.stopFrequencyHz;
        acquisition.frequencyAxis.points = channel->sweep.points;
        acquisition.ifBandwidthHz =
            static_cast<std::uint32_t>(channel->sweep.ifBandwidthHz);
        acquisition.powerDbm = channel->sweep.powerDbm;
    }
    const auto acquisitionChanged = !sameAcquisitionPlan(
        acquisition, plan_.acquisition);
    auto publication = catalog_.prepare(
        candidate, candidate.stateRevision, acquisitionChanged);
    if (std::holds_alternative<TracePublicationCatalogError>(publication)) {
        return SweepRuntimeConfigurationError{
            SweepRuntimeConfigurationErrorCode::TraceConfigurationRejected};
    }
    auto prepared = std::get<PreparedTracePublicationPlan>(
        std::move(publication));
    auto pending = std::make_unique<PendingSweepRuntimeConfiguration>(
        PendingSweepRuntimeConfiguration{
            SweepRuntimePlan{std::move(acquisition), prepared.candidate_,
                             plan_.maximumPointsPerChunk, plan_.execution},
            std::move(prepared)});
    return PreparedSweepRuntimeConfiguration{std::make_unique<
        detail::PreparedSweepRuntimeConfigurationState>(
        detail::PreparedSweepRuntimeConfigurationState{
            this, std::move(gate), std::move(pending)})};
}

void SweepRuntimeImpl::commitConfiguration(
    PreparedSweepRuntimeConfiguration prepared) noexcept {
    auto state = std::move(prepared.state_);
    if (state == nullptr || state->owner != this || !state->gate.owns_lock()) {
        std::terminate();
    }
    static_assert(std::is_nothrow_move_assignable_v<
        decltype(pendingConfiguration_)>);
    pendingConfiguration_ = std::move(state->pending);
    snapshot_.configuredStateRevision =
        pendingConfiguration_->plan.publication->stateRevision;
    if (snapshot_.phase == SweepRuntimePhase::Hold &&
        !pendingOperation_.has_value() && !activeRequest_.has_value()) {
        // Hold is already a safe Sweep boundary; applying here prevents a
        // configuration from remaining pending until a future Restart.
        applyPendingConfiguration();
    }
    static_assert(std::is_nothrow_destructible_v<
        detail::PreparedSweepRuntimeConfigurationState>);
    static_assert(noexcept(state.reset()));
    state.reset();
    changed_.notify_all();
}

void SweepRuntimeImpl::applyPendingConfiguration() {
    if (pendingConfiguration_ == nullptr) {
        return;
    }
    auto committed = SweepGenerationTransaction::commit(
        catalog_, previews_, pendingConfiguration_->publication);
    if (std::holds_alternative<SweepGenerationCommitError>(committed)) {
        throw std::logic_error{"InternalInvariantViolation: generation commit"};
    }
    static_assert(std::is_nothrow_move_assignable_v<SweepRuntimePlan>);
    plan_ = std::move(pendingConfiguration_->plan);
    pendingConfiguration_.reset();
    snapshot_.appliedStateRevision = plan_.publication->stateRevision;
    snapshot_.appliedGeneration = plan_.publication->generation;
}

}  // namespace vna::application::internal
