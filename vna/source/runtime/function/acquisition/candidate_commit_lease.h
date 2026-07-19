#pragma once

#include "runtime/core/base/strong_id.h"
#include "runtime/function/operation/operation_runtime.h"
#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vna::store {
class CompletedSweepBundle;
class InstrumentStore;
}

namespace vna::acquisition {

/// 正式 A 层完整扫频快照 ID；0 表示尚未签发。
using CompletedSweepId = core::StrongId<struct CompletedSweepIdTag>;
/// 一次完整逻辑扫频的稳定身份；0 表示无效。
using LogicalSweepId = core::StrongId<struct LogicalSweepIdTag>;

/// 单个 A 快照在当前产品配置中允许的最大点数。
constexpr std::size_t kMaximumCompletedSweepPoints = 201U;

/// 单板 Run 对一份完整 A candidate 的有界来源证据。
struct BoardRunEvidence final {
    /// Prepare 返回且经本地收窄接受的实际执行清单。
    board::PreparedExecutionManifest manifest{};
    /// 交付观测的 Board Run 身份。
    board::BoardRunId run_id{};
    /// 与 run_id 配对的 generation。
    board::RunGeneration generation{};
    /// 正式入射波 a 的完整覆盖点数。
    std::uint32_t incident_points{0U};
    /// 正式响应波 b 的完整覆盖点数。
    std::uint32_t response_points{0U};
    /// terminal 前实际交付的数据块数。
    std::uint32_t delivered_chunks{0U};
    /// 已验证恰好一个匹配的 Completed terminal。
    bool unique_success_terminal{false};
};

/// Builder 已接管的一项必需观测及其唯一 payload owner。
struct CandidateObservationLease final {
    /// Manifest 中对应的观测声明。
    board::PreparedObservationSpec spec{};
    /// 本块的非 0 序号。
    board::ChunkSequence sequence{};
    /// 唯一拥有正式复数样本的 move-only lease。
    board::AcquisitionChunkLease payload;
    /// 与本块所有点同路传播的质量标志。
    board::ChunkQuality quality{};
};

/// 从 Acquisition worker return 持续到 Store commit 或显式 abort 的候选租约。
///
/// 候选只暴露关联身份，不暴露尚未发布的样本。内部 payload owner 只能移动给
/// InstrumentStore 完成同步提交，或由 abort() 释放；因此 worker 与 Store 之间
/// 不存在无主或可查询的半成品窗口。
class CandidateCommitLease final {
public:
    /// 转移 candidate metadata、实际轴和全部 chunk owner。
    /// @param other 所有权来源；构造后 other 失效。
    CandidateCommitLease(CandidateCommitLease&& other) noexcept;
    /// 先 abort 当前候选，再转移来源。
    /// @param other 所有权来源；赋值后 other 失效。
    /// @return 当前对象引用。
    CandidateCommitLease& operator=(CandidateCommitLease&& other) noexcept;
    /// CandidateCommitLease 拥有唯一 chunk owner，禁止复制。
    CandidateCommitLease(const CandidateCommitLease&) = delete;
    /// CandidateCommitLease 拥有唯一 chunk owner，禁止复制赋值。
    CandidateCommitLease& operator=(const CandidateCommitLease&) = delete;
    /// 释放尚未 commit/abort 的全部 chunk owner；不发布任何 Store 事实。
    ~CandidateCommitLease() = default;

    /// @return 候选仍拥有有效身份和全部必需观测时返回 true。
    bool valid() const noexcept;
    /// @return 预签发且提交成功后公开的 A snapshot 身份。
    CompletedSweepId snapshot_id() const noexcept { return snapshot_id_; }
    /// @return 本候选所属 Logical Sweep 身份。
    LogicalSweepId logical_sweep_id() const noexcept { return logical_sweep_id_; }
    /// @return 与 Accepted Operation 关联的 Runtime WorkId 事实。
    runtime::WorkId work() const noexcept { return work_; }
    /// @return 与 Accepted Operation 关联的冻结计划摘要。
    core::StrongDigest plan_digest() const noexcept { return plan_digest_; }

    /// 放弃尚未提交的候选并释放全部 chunk owner。
    /// @return 首次放弃有效候选时返回 true；已移动或重复调用返回 false。
    bool abort() noexcept;

private:
    friend class NetworkObservationBuilder;
    friend class store::CompletedSweepBundle;
    friend class store::InstrumentStore;

    CandidateCommitLease(
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
        BoardRunEvidence evidence) noexcept;

    void invalidate() noexcept;

    CompletedSweepId snapshot_id_{};
    LogicalSweepId logical_sweep_id_{};
    runtime::WorkId work_{};
    core::StrongDigest plan_digest_{};
    board::PreparedExecutionManifest manifest_{};
    std::array<double, kMaximumCompletedSweepPoints> axis_hz_{};
    std::array<
        std::optional<CandidateObservationLease>,
        board::kMaximumPreparedObservations> observations_{};
    std::uint32_t observation_count_{0U};
    BoardRunEvidence evidence_{};
    bool valid_{false};
};

}  // namespace vna::acquisition
