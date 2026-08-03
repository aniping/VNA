#pragma once

#include <cstdint>
#include <optional>

#include <vna/acquisition/continuous_acquisition.hpp>
#include <vna/domain/instrument.hpp>

namespace vna::application {

// Publication bookkeeping folds into Calculation so protocols never expose
// transport or repository details as operator-visible instrument state.
enum class SweepUserPhase {
    Hold,
    Preparing,
    Sweeping,
    Calculation,
    Failed,
};

struct SweepAcquisitionProgress {
    std::uint64_t completedPoints{};
    std::uint64_t totalPoints{};
    friend bool operator==(
        const SweepAcquisitionProgress&,
        const SweepAcquisitionProgress&) = default;
};

// Runtime owns this truth; the Exchange retains a copy so reconnecting and slow
// consumers recover without reading Runtime under a second lock order.
struct SweepRuntimeDisplayStatus {
    std::uint64_t generation;
    domain::ChannelId channelId;
    std::uint64_t stateRevision;
    std::optional<acquisition::SweepId> sweepId;
    SweepUserPhase userPhase;
    SweepAcquisitionProgress progress;
    bool firstSweepAfterConfiguration;
    friend bool operator==(
        const SweepRuntimeDisplayStatus&,
        const SweepRuntimeDisplayStatus&) = default;
};

}  // namespace vna::application
