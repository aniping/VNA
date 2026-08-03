#pragma once

#include <vna/application/sweep_runtime_status.hpp>

namespace vna::test {

inline application::SweepRuntimeDisplayStatus testSweepStatus(
    const acquisition::ContinuousAcquisitionPlan& plan,
    std::uint64_t stateRevision = 0) {
    const auto total = static_cast<std::uint64_t>(plan.frequencyAxis.points) *
        plan.sourcePorts.size();
    return {
        1,
        domain::ChannelId{1},
        stateRevision,
        std::nullopt,
        application::SweepUserPhase::Preparing,
        {0, total},
        false,
    };
}

inline application::SweepRuntimeDisplayStatus testSweepStatus() {
    return {1, domain::ChannelId{1}, 0, std::nullopt,
            application::SweepUserPhase::Preparing, {0, 6}, false};
}

}  // namespace vna::test
