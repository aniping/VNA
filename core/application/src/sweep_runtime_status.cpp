#include <vna/application/sweep_runtime.hpp>

#include "sweep_runtime_internal.hpp"

#include <limits>
#include <stdexcept>

namespace vna::application {
namespace {

std::uint64_t totalAcquisitionPoints(
    const acquisition::ContinuousAcquisitionPlan& plan) {
    if (plan.frequencyAxis.points == 0 || plan.sourcePorts.empty() ||
        plan.sourcePorts.size() >
            std::numeric_limits<std::uint64_t>::max() /
                plan.frequencyAxis.points) {
        throw std::invalid_argument{"invalid sweep acquisition workload"};
    }
    return static_cast<std::uint64_t>(plan.frequencyAxis.points) *
        plan.sourcePorts.size();
}

}  // namespace

SweepRuntimeDisplayStatus initialSweepRuntimeStatus(
    const SweepRuntimePlan& plan) {
    if (plan.publication == nullptr || plan.publication->generation == 0 ||
        plan.publication->channelId.value() == 0) {
        throw std::invalid_argument{"invalid sweep runtime publication"};
    }
    const auto total = totalAcquisitionPoints(plan.acquisition);
    const auto hold = plan.execution.mode == domain::SweepMode::Single;
    return {
        plan.publication->generation,
        plan.publication->channelId,
        plan.publication->stateRevision,
        std::nullopt,
        hold ? SweepUserPhase::Hold : SweepUserPhase::Preparing,
        {hold ? total : 0, total},
        false,
    };
}

}  // namespace vna::application

namespace vna::application::internal {

SweepRuntimeDisplayStatus SweepRuntimeImpl::displayStatusLocked() const {
    return {
        snapshot_.appliedGeneration,
        plan_.publication->channelId,
        snapshot_.appliedStateRevision,
        snapshot_.activeSweepId,
        snapshot_.phase,
        snapshot_.progress,
        snapshot_.firstSweepAfterConfiguration,
    };
}

void SweepRuntimeImpl::setDisplayStatusLocked(
    SweepUserPhase phase,
    std::optional<acquisition::SweepId> sweepId,
    std::uint64_t completedPoints) noexcept {
    snapshot_.phase = phase;
    snapshot_.activeSweepId = sweepId;
    snapshot_.progress.completedPoints = completedPoints;
}

void SweepRuntimeImpl::invalidateLocked(
    SweepPreviewIdentity identity) noexcept {
    static_cast<void>(previews_.invalidateForRuntime(
        identity, displayStatusLocked()));
}

}  // namespace vna::application::internal
