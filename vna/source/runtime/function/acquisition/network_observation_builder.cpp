#include "runtime/function/acquisition/network_observation_builder.h"

#include <cstddef>
#include <utility>

namespace vna::acquisition {

NetworkObservationBuilder::NetworkObservationBuilder(
    board::PreparedExecutionManifest manifest,
    board::BoardRunId run,
    board::RunGeneration generation) noexcept
    : manifest_(manifest), run_(run), generation_(generation) {
    if (!manifest_is_valid()) {
        error_ = NetworkObservationError{NetworkObservationErrc::InvalidManifest};
    }
}

board::ChunkIngressDisposition NetworkObservationBuilder::accept(
    board::ReceiverObservationChunk&& chunk) noexcept {
    // 无条件接管 Board 已交付的 payload；协议拒绝只改变 disposition，不能把
    // owner 退回 Adapter，也不能让调用者在返回后继续观察有效 lease。
    auto owned = std::move(chunk);
    if (sealed_ || error_.has_value() || terminal_.has_value()) {
        (void)remember(NetworkObservationErrc::InvalidChunk);
        return board::ChunkIngressDisposition::AbortRunProtocolViolation;
    }
    const auto observation_index = find_observation(owned.wave);
    const bool identity_matches = observation_index.has_value() &&
        owned.manifest_id == manifest_.id &&
        owned.prepared_id == manifest_.prepared_id &&
        owned.run_id == run_ && owned.run_generation == generation_ &&
        owned.sequence.valid() && owned.payload.valid() &&
        owned.point_begin == 0U &&
        owned.payload.size() ==
            manifest_.required_observations[*observation_index].point_count;
    if (!identity_matches || sequence_already_used(owned.sequence)) {
        (void)remember(NetworkObservationErrc::InvalidChunk);
        return board::ChunkIngressDisposition::AbortRunProtocolViolation;
    }
    if (observations_[*observation_index].has_value()) {
        (void)remember(NetworkObservationErrc::DuplicateObservation);
        return board::ChunkIngressDisposition::AbortRunProtocolViolation;
    }

    observations_[*observation_index].emplace(CandidateObservationLease{
        manifest_.required_observations[*observation_index],
        owned.sequence,
        std::move(owned.payload),
        owned.quality});
    return board::ChunkIngressDisposition::Accepted;
}

bool NetworkObservationBuilder::record_terminal(
    board::BoardRunTerminal terminal) noexcept {
    if (sealed_ || terminal_.has_value() || terminal.run_id != run_ ||
        terminal.generation != generation_) {
        (void)remember(NetworkObservationErrc::InvalidTerminal);
        return false;
    }
    terminal_ = terminal;
    return true;
}

core::Result<CandidateCommitLease, NetworkObservationError>
NetworkObservationBuilder::seal(
    CompletedSweepId snapshot_id,
    LogicalSweepId logical_sweep_id,
    runtime::WorkId work,
    core::StrongDigest plan_digest) noexcept {
    const auto fail = [&](NetworkObservationErrc code) {
        return core::Result<CandidateCommitLease, NetworkObservationError>::failure(
            remember(code));
    };
    if (sealed_) {
        return fail(NetworkObservationErrc::AlreadySealed);
    }
    if (error_.has_value()) {
        return core::Result<CandidateCommitLease, NetworkObservationError>::failure(
            *error_);
    }
    if (!snapshot_id.valid() || !logical_sweep_id.valid() || !work.valid() ||
        !plan_digest.valid() || plan_digest != manifest_.intent_digest) {
        return fail(NetworkObservationErrc::InvalidManifest);
    }
    if (!terminal_.has_value() ||
        terminal_->kind != board::RunTerminalKind::Completed) {
        return fail(NetworkObservationErrc::InvalidTerminal);
    }
    for (std::size_t index = 0U;
         index < manifest_.required_observation_count;
         ++index) {
        if (!observations_[index].has_value()) {
            return fail(NetworkObservationErrc::IncompleteCoverage);
        }
    }
    if (terminal_->delivered_chunks != manifest_.required_observation_count) {
        return fail(NetworkObservationErrc::IncompleteCoverage);
    }

    std::array<double, kMaximumCompletedSweepPoints> axis_hz{};
    const auto point_count = manifest_.actual_point_count;
    if (point_count == 1U) {
        axis_hz[0U] = manifest_.actual_start_hz;
    } else {
        const auto step =
            (manifest_.actual_stop_hz - manifest_.actual_start_hz) /
            static_cast<double>(point_count - 1U);
        for (std::size_t index = 0U; index < point_count; ++index) {
            axis_hz[index] = manifest_.actual_start_hz +
                step * static_cast<double>(index);
        }
    }

    BoardRunEvidence evidence{};
    evidence.manifest = manifest_;
    evidence.run_id = run_;
    evidence.generation = generation_;
    evidence.delivered_chunks = terminal_->delivered_chunks;
    evidence.unique_success_terminal = true;
    for (std::size_t index = 0U;
         index < manifest_.required_observation_count;
         ++index) {
        if (manifest_.required_observations[index].wave ==
            board::ReceiverWave::IncidentA) {
            evidence.incident_points =
                manifest_.required_observations[index].point_count;
        } else if (manifest_.required_observations[index].wave ==
                   board::ReceiverWave::ResponseB) {
            evidence.response_points =
                manifest_.required_observations[index].point_count;
        }
    }

    sealed_ = true;
    return core::Result<CandidateCommitLease, NetworkObservationError>::success(
        CandidateCommitLease{
            snapshot_id,
            logical_sweep_id,
            work,
            plan_digest,
            manifest_,
            axis_hz,
            std::move(observations_),
            manifest_.required_observation_count,
            evidence});
}

bool NetworkObservationBuilder::manifest_is_valid() const noexcept {
    if (!manifest_.id.valid() || !manifest_.prepared_id.valid() ||
        !manifest_.manifest_digest.valid() || !run_.valid() ||
        !generation_.valid() || manifest_.actual_point_count == 0U ||
        manifest_.actual_point_count > kMaximumCompletedSweepPoints ||
        manifest_.required_observation_count == 0U ||
        manifest_.required_observation_count > observations_.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < manifest_.required_observation_count;
         ++index) {
        const auto& required = manifest_.required_observations[index];
        if (required.point_count != manifest_.actual_point_count) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (manifest_.required_observations[previous].wave == required.wave) {
                return false;
            }
        }
    }
    return true;
}

std::optional<std::size_t> NetworkObservationBuilder::find_observation(
    board::ReceiverWave wave) const noexcept {
    for (std::size_t index = 0U;
         index < manifest_.required_observation_count;
         ++index) {
        if (manifest_.required_observations[index].wave == wave) {
            return index;
        }
    }
    return std::nullopt;
}

bool NetworkObservationBuilder::sequence_already_used(
    board::ChunkSequence sequence) const noexcept {
    for (const auto& observation : observations_) {
        if (observation.has_value() && observation->sequence == sequence) {
            return true;
        }
    }
    return false;
}

NetworkObservationError NetworkObservationBuilder::remember(
    NetworkObservationErrc code) noexcept {
    if (!error_.has_value()) {
        error_ = NetworkObservationError{code};
    }
    return *error_;
}

}  // namespace vna::acquisition
