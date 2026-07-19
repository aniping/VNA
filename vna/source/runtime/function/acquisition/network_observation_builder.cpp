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
    const auto observation_index = find_observation(
        owned.source_state, owned.receiver_path, owned.wave);
    const auto payload_size = owned.payload.size();
    const bool identity_matches = observation_index.has_value() &&
        owned.manifest_id == manifest_.id &&
        owned.prepared_id == manifest_.prepared_id &&
        owned.run_id == run_ && owned.run_generation == generation_ &&
        owned.sequence.valid() && owned.payload.valid() &&
        payload_size <= board::kMaximumContractChunkSamples &&
        owned.point_begin <
            manifest_.required_observations[*observation_index].point_count &&
        payload_size <=
            manifest_.required_observations[*observation_index].point_count -
                owned.point_begin;
    if (!identity_matches || sequence_already_used(owned.sequence)) {
        (void)remember(NetworkObservationErrc::InvalidChunk);
        return board::ChunkIngressDisposition::AbortRunProtocolViolation;
    }
    if (!observations_[*observation_index].has_value()) {
        CandidateObservationLease observation{};
        observation.spec =
            manifest_.required_observations[*observation_index];
        observations_[*observation_index].emplace(std::move(observation));
    }
    auto& observation = *observations_[*observation_index];
    if (observation.chunk_count >= observation.chunks.size() ||
        chunk_count_ >= chunk_ledger_.size() ||
        overlaps_existing(
            observation,
            owned.point_begin,
            static_cast<std::uint32_t>(payload_size))) {
        (void)remember(NetworkObservationErrc::InvalidChunk);
        return board::ChunkIngressDisposition::AbortRunProtocolViolation;
    }

    // 先记下不持有 payload 的到达账本，再把唯一 lease 移入所属观测。最终样本
    // 位置只由 point_begin 决定，因此 callback 乱序不会改变正式快照。
    chunk_ledger_[chunk_count_] = BoardChunkEvidence{
        owned.source_state,
        owned.receiver_path,
        owned.wave,
        owned.sequence,
        owned.point_begin,
        static_cast<std::uint32_t>(payload_size)};
    ++chunk_count_;
    observation.chunks[observation.chunk_count].emplace(
        CandidateObservationChunkLease{
            owned.sequence,
            owned.point_begin,
            std::move(owned.payload),
            owned.quality});
    ++observation.chunk_count;
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
        if (!observations_[index].has_value() ||
            !has_complete_coverage(*observations_[index])) {
            return fail(NetworkObservationErrc::IncompleteCoverage);
        }
    }
    if (terminal_->delivered_chunks != chunk_count_) {
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
    evidence.chunks = chunk_ledger_;
    evidence.chunk_count = chunk_count_;
    evidence.terminal = *terminal_;
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
        if (!required.source_state.valid() || !required.receiver_path.valid() ||
            required.point_count != manifest_.actual_point_count) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            const auto& previous_required =
                manifest_.required_observations[previous];
            if (previous_required.source_state == required.source_state &&
                previous_required.receiver_path == required.receiver_path &&
                previous_required.wave == required.wave) {
                return false;
            }
        }
    }
    return true;
}

std::optional<std::size_t> NetworkObservationBuilder::find_observation(
    board::SourceStateId source_state,
    board::ReceiverPathId receiver_path,
    board::ReceiverWave wave) const noexcept {
    for (std::size_t index = 0U;
         index < manifest_.required_observation_count;
         ++index) {
        const auto& required = manifest_.required_observations[index];
        if (required.source_state == source_state &&
            required.receiver_path == receiver_path &&
            required.wave == wave) {
            return index;
        }
    }
    return std::nullopt;
}

bool NetworkObservationBuilder::sequence_already_used(
    board::ChunkSequence sequence) const noexcept {
    for (std::size_t index = 0U; index < chunk_count_; ++index) {
        if (chunk_ledger_[index].sequence == sequence) {
            return true;
        }
    }
    return false;
}

bool NetworkObservationBuilder::overlaps_existing(
    const CandidateObservationLease& observation,
    std::uint32_t point_begin,
    std::uint32_t point_count) const noexcept {
    const auto point_end = point_begin + point_count;
    for (std::size_t index = 0U; index < observation.chunk_count; ++index) {
        const auto& chunk = *observation.chunks[index];
        const auto existing_end =
            chunk.point_begin + static_cast<std::uint32_t>(chunk.payload.size());
        if (point_begin < existing_end && chunk.point_begin < point_end) {
            return true;
        }
    }
    return false;
}

bool NetworkObservationBuilder::has_complete_coverage(
    const CandidateObservationLease& observation) const noexcept {
    std::uint32_t covered_points{0U};
    for (std::size_t index = 0U; index < observation.chunk_count; ++index) {
        if (!observation.chunks[index].has_value()) {
            return false;
        }
        covered_points += static_cast<std::uint32_t>(
            observation.chunks[index]->payload.size());
    }
    // accept() 已保证每块在范围内且互不重叠，因此总覆盖等于 Manifest 点数
    // 当且仅当没有缺口。
    return covered_points == observation.spec.point_count;
}

NetworkObservationError NetworkObservationBuilder::remember(
    NetworkObservationErrc code) noexcept {
    if (!error_.has_value()) {
        error_ = NetworkObservationError{code};
    }
    return *error_;
}

}  // namespace vna::acquisition
