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
        const SweepAcquisitionProgress& left,
        const SweepAcquisitionProgress& right) {
        return left.completedPoints == right.completedPoints &&
            left.totalPoints == right.totalPoints;
    }
    friend bool operator!=(
        const SweepAcquisitionProgress& left,
        const SweepAcquisitionProgress& right) {
        return !(left == right);
    }
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
        const SweepRuntimeDisplayStatus& left,
        const SweepRuntimeDisplayStatus& right) {
        return left.generation == right.generation &&
            left.channelId == right.channelId &&
            left.stateRevision == right.stateRevision &&
            left.sweepId == right.sweepId &&
            left.userPhase == right.userPhase &&
            left.progress == right.progress &&
            left.firstSweepAfterConfiguration ==
                right.firstSweepAfterConfiguration;
    }
    friend bool operator!=(
        const SweepRuntimeDisplayStatus& left,
        const SweepRuntimeDisplayStatus& right) {
        return !(left == right);
    }
};

}  // namespace vna::application
