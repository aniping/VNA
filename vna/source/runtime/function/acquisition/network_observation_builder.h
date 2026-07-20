#pragma once

#include "runtime/core/base/result.h"
#include "runtime/function/acquisition/candidate_commit_lease.h"
#include "runtime/function/acquisition/network_observation_error.h"
#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vna::acquisition {

/// 按 Prepared Manifest 必需观测图组装单板完整接收机波量的唯一长期 owner。
///
/// Builder 以 Manifest 的 typed observation set 为唯一形状来源，按 point range
/// 组装乱序数据块；callback 顺序只进入证据账本，不决定最终点位置。Builder 取得
/// 每个 AcquisitionChunkLease 后，Adapter 不再拥有其 payload。
class NetworkObservationBuilder final {
public:
    /// 建立与一次实际 Prepared execution 和 Run 绑定的空 Builder。
    /// @param manifest Board Prepare 返回的实际事实；按值冻结。
    /// @param run 后续所有 chunk/terminal 必须匹配的 Run ID。
    /// @param generation 后续所有 chunk/terminal 必须匹配的 Run generation。
    NetworkObservationBuilder(
        board::PreparedExecutionManifest manifest,
        board::BoardRunId run,
        board::RunGeneration generation) noexcept;
    /// 转移尚未密封的 Manifest、terminal 与全部 chunk owner。
    /// @param other 所有权来源；构造完成后来源只可析构或重新赋值。
    NetworkObservationBuilder(NetworkObservationBuilder&& other) noexcept = default;
    /// 先释放当前未密封 owner，再转移来源 Builder。
    /// @param other 所有权来源；赋值完成后来源只可析构或重新赋值。
    /// @return 当前 Builder 引用；目标原有未密封 payload 在赋值中释放。
    NetworkObservationBuilder& operator=(NetworkObservationBuilder&& other) noexcept = default;
    /// Builder 是正式 chunk 的唯一长期 owner，禁止复制。
    NetworkObservationBuilder(const NetworkObservationBuilder&) = delete;
    /// Builder 是正式 chunk 的唯一长期 owner，禁止复制赋值。
    NetworkObservationBuilder& operator=(const NetworkObservationBuilder&) = delete;

    /// 接管一项 Board 正式观测。
    /// @param chunk 身份与 payload 所有权一并转移；拒绝时 payload 也由 Builder
    ///        在返回前释放，Adapter 不应再次使用。
    /// @return Accepted 表示 chunk 已由唯一 Builder 保管；协议错误返回
    ///         AbortRunProtocolViolation，当前切片不在回调中申请额外容量。
    board::ChunkIngressDisposition accept(
        board::ReceiverObservationChunk&& chunk) noexcept;

    /// 把 Ingress 已接管但未能入队的正式块记入同一失败账本。
    /// @param chunk 不持有 payload 的来源、序号和声明点范围副本。
    /// @param disposition Ingress 返回的容量或协议拒绝；Accepted 不是合法入参。
    /// @return 首项稳定失败证据；不接管任何 Buffer 所有权，后续错误不覆盖它。
    NetworkObservationError record_ingress_rejection(
        BoardChunkEvidence chunk,
        board::ChunkIngressDisposition disposition) noexcept;

    /// 记录唯一 Run terminal。
    /// @param terminal 必须与构造时的 run/generation 匹配；按值保存。
    /// @return 首次匹配 terminal 返回 true；重复或错误身份返回 false。
    bool record_terminal(board::BoardRunTerminal terminal) noexcept;

    /// 在完整观测与唯一成功 terminal 均闭合后生成不可查询的 commit candidate。
    /// @param snapshot_id 首次派发前预签发的正式 A ID。
    /// @param logical_sweep_id 本次逻辑扫频 ID。
    /// @param work 对应 Accepted Operation 的 Runtime correlation。
    /// @param plan_digest 对应 Accepted Operation 的冻结计划摘要。
    /// @return 成功时转移全部 chunk owner；失败时返回稳定错误且不产生 candidate。
    core::Result<CandidateCommitLease, NetworkObservationError> seal(
        CompletedSweepId snapshot_id,
        LogicalSweepId logical_sweep_id,
        runtime::WorkId work,
        core::StrongDigest plan_digest) noexcept;

    /// @return 最近一次拒绝的稳定原因；尚无错误时为空。
    std::optional<NetworkObservationError> error() const noexcept {
        return error_;
    }

private:
    bool manifest_is_valid() const noexcept;
    std::optional<std::size_t> find_observation(
        board::SourceStateId source_state,
        board::ReceiverPathId receiver_path,
        board::ReceiverWave wave) const noexcept;
    bool sequence_already_used(board::ChunkSequence sequence) const noexcept;
    bool overlaps_existing(
        const CandidateObservationLease& observation,
        std::uint32_t point_begin,
        std::uint32_t point_count) const noexcept;
    bool duplicates_existing_range(
        const CandidateObservationLease& observation,
        std::uint32_t point_begin,
        std::uint32_t point_count) const noexcept;
    bool has_complete_coverage(
        const CandidateObservationLease& observation) const noexcept;
    std::optional<std::size_t> first_incomplete_observation() const noexcept;
    ObservationCoverageSummary summarize_coverage(
        std::optional<std::size_t> observation_index) const noexcept;
    NetworkObservationError make_error(
        NetworkObservationErrc code,
        std::optional<std::size_t> observation_index,
        const BoardChunkEvidence* offending_chunk) const noexcept;
    NetworkObservationError remember(NetworkObservationError error) noexcept;

    board::PreparedExecutionManifest manifest_{};
    board::BoardRunId run_{};
    board::RunGeneration generation_{};
    std::array<
        std::optional<CandidateObservationLease>,
        board::kMaximumPreparedObservations> observations_{};
    std::array<BoardChunkEvidence, board::kMaximumRunChunks> chunk_ledger_{};
    std::uint32_t chunk_count_{0U};
    std::optional<board::BoardRunTerminal> terminal_{};
    std::optional<NetworkObservationError> error_{};
    bool sealed_{false};
};

}  // namespace vna::acquisition
