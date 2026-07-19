#pragma once

#include "runtime/core/base/result.h"
#include "runtime/function/acquisition/candidate_commit_lease.h"
#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vna::acquisition {

/// 单板观测构建失败的稳定分类。
enum class NetworkObservationErrc {
    /// Manifest 的实际轴或必需观测图不完整、重复或超出当前有界切片。
    InvalidManifest,
    /// 数据块身份、序号、点范围或 payload 与 Manifest/Run 不匹配。
    InvalidChunk,
    /// 相同必需观测被交付多次。
    DuplicateObservation,
    /// Run terminal 身份、数量或种类违反契约。
    InvalidTerminal,
    /// 成功 terminal 到达时仍缺少必需观测或完整点覆盖。
    IncompleteCoverage,
    /// Builder 已经密封，不能再次接收或密封。
    AlreadySealed
};

/// NetworkObservationBuilder 返回的类型化错误。
struct NetworkObservationError final {
    NetworkObservationErrc code{NetworkObservationErrc::InvalidManifest};
};

/// 按 Prepared Manifest 必需观测图组装单板完整接收机波量的唯一长期 owner。
///
/// 当前工单只接受每项观测一个完整块；多块、乱序和重叠账本由工单 04 扩展。
/// Builder 取得每个 AcquisitionChunkLease 后，Adapter 不再拥有其 payload。
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
        board::ReceiverWave wave) const noexcept;
    bool sequence_already_used(board::ChunkSequence sequence) const noexcept;
    NetworkObservationError remember(NetworkObservationErrc code) noexcept;

    board::PreparedExecutionManifest manifest_{};
    board::BoardRunId run_{};
    board::RunGeneration generation_{};
    std::array<
        std::optional<CandidateObservationLease>,
        board::kMaximumPreparedObservations> observations_{};
    std::optional<board::BoardRunTerminal> terminal_{};
    std::optional<NetworkObservationError> error_{};
    bool sealed_{false};
};

}  // namespace vna::acquisition
