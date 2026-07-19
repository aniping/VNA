#include "runtime/function/acquisition/candidate_commit_lease.h"

#include <utility>

namespace vna::acquisition {

CandidateCommitLease::CandidateCommitLease(
    CompletedSweepId snapshot_id,
    LogicalSweepId logical_sweep_id,
    runtime::WorkId work,
    core::StrongDigest plan_digest,
    board::PreparedExecutionManifest manifest,
    std::array<double, kMaximumCompletedSweepPoints> axis_hz,
    std::array<
        std::optional<CandidateObservationLease>,
        board::kMaximumPreparedObservations>&& observations,
    std::uint32_t observation_count,
    BoardRunEvidence evidence) noexcept
    : snapshot_id_(snapshot_id),
      logical_sweep_id_(logical_sweep_id),
      work_(work),
      plan_digest_(plan_digest),
      manifest_(manifest),
      axis_hz_(axis_hz),
      observations_(std::move(observations)),
      observation_count_(observation_count),
      evidence_(evidence),
      valid_(snapshot_id.valid() && logical_sweep_id.valid() && work.valid() &&
             plan_digest.valid() && manifest.id.valid() &&
             observation_count > 0U &&
             observation_count <= observations_.size()) {}

CandidateCommitLease::CandidateCommitLease(
    CandidateCommitLease&& other) noexcept
    : snapshot_id_(other.snapshot_id_),
      logical_sweep_id_(other.logical_sweep_id_),
      work_(other.work_),
      plan_digest_(other.plan_digest_),
      manifest_(other.manifest_),
      axis_hz_(other.axis_hz_),
      observations_(std::move(other.observations_)),
      observation_count_(other.observation_count_),
      evidence_(other.evidence_),
      valid_(other.valid_) {
    other.invalidate();
}

CandidateCommitLease& CandidateCommitLease::operator=(
    CandidateCommitLease&& other) noexcept {
    if (this != &other) {
        (void)abort();
        snapshot_id_ = other.snapshot_id_;
        logical_sweep_id_ = other.logical_sweep_id_;
        work_ = other.work_;
        plan_digest_ = other.plan_digest_;
        manifest_ = other.manifest_;
        axis_hz_ = other.axis_hz_;
        observations_ = std::move(other.observations_);
        observation_count_ = other.observation_count_;
        evidence_ = other.evidence_;
        valid_ = other.valid_;
        other.invalidate();
    }
    return *this;
}

bool CandidateCommitLease::valid() const noexcept {
    if (!valid_ || observation_count_ == 0U ||
        observation_count_ > observations_.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < observation_count_; ++index) {
        if (!observations_[index].has_value() ||
            observations_[index]->chunk_count == 0U ||
            observations_[index]->chunk_count >
                observations_[index]->chunks.size()) {
            return false;
        }
        for (std::size_t chunk_index = 0U;
             chunk_index < observations_[index]->chunk_count;
             ++chunk_index) {
            if (!observations_[index]->chunks[chunk_index].has_value() ||
                !observations_[index]->chunks[chunk_index]->payload.valid()) {
                return false;
            }
        }
    }
    return true;
}

bool CandidateCommitLease::abort() noexcept {
    if (!valid_) {
        return false;
    }
    for (auto& observation : observations_) {
        observation.reset();
    }
    invalidate();
    return true;
}

void CandidateCommitLease::invalidate() noexcept {
    snapshot_id_ = CompletedSweepId{};
    logical_sweep_id_ = LogicalSweepId{};
    work_ = runtime::WorkId{};
    plan_digest_ = core::StrongDigest{};
    manifest_ = board::PreparedExecutionManifest{};
    axis_hz_ = {};
    observation_count_ = 0U;
    evidence_ = BoardRunEvidence{};
    valid_ = false;
}

}  // namespace vna::acquisition
