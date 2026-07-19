#include "runtime/resource/store/completed_sweep_bundle.h"

#include <cstddef>

namespace vna::store {

CompletedSweepBundle::CompletedSweepBundle(
    OperationId operation,
    std::uint64_t revision,
    const acquisition::CandidateCommitLease& candidate) noexcept
    : operation_(operation),
      id_(candidate.snapshot_id_),
      logical_sweep_id_(candidate.logical_sweep_id_),
      revision_(revision),
      point_count_(candidate.manifest_.actual_point_count),
      axis_hz_(candidate.axis_hz_),
      observation_count_(candidate.observation_count_),
      evidence_(candidate.evidence_) {
    for (std::size_t observation_index = 0U;
         observation_index < observation_count_;
         ++observation_index) {
        const auto& source = *candidate.observations_[observation_index];
        auto& destination = observations_[observation_index];
        destination.wave = source.spec.wave;
        destination.point_count = source.spec.point_count;
        for (std::size_t point = 0U; point < source.payload.size(); ++point) {
            destination.values[point] = source.payload[point];
            destination.quality_flags[point] = source.quality.flags;
        }
    }
}

}  // namespace vna::store
