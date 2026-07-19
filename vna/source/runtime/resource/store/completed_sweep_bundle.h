#pragma once

#include "runtime/core/base/strong_id.h"
#include "runtime/function/acquisition/candidate_commit_lease.h"
#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vna::store {

/// 对外可见操作的唯一标识；0 为无效值。
using OperationId = core::StrongId<struct OperationIdTag>;

/// 一项已发布接收机波量及其逐点质量副本。
struct CompletedReceiverObservation final {
    /// 接收机波量身份。
    board::ReceiverWave wave{board::ReceiverWave::IncidentA};
    /// values/quality_flags 中有效点数。
    std::uint32_t point_count{0U};
    /// 按实际激励轴顺序排列的不可变复数值副本。
    std::array<
        board::ComplexSample,
        acquisition::kMaximumCompletedSweepPoints> values{};
    /// 与 values 同索引的质量标志；当前 Mock 块级标志扩展到每个实际点。
    std::array<
        std::uint32_t,
        acquisition::kMaximumCompletedSweepPoints> quality_flags{};
};

/// Store 原子发布并按值查询的一份不可变 A 层完整扫频快照。
///
/// 对象只提供只读访问；InstrumentStore 内部保存独立副本，调用者修改查询返回值
/// 不会改变已发布事实，也不会取得 Store 内部裸 Buffer 指针。
class CompletedSweepBundle final {
public:
    /// 建立无效空值，仅供固定容量 Store Slot 初始化；不会成为可查询 publication。
    CompletedSweepBundle() noexcept = default;
    /// 复制公开快照值；副本不共享 Store 内部可变状态或裸 Buffer。
    /// @param other 只读值来源；调用后来源保持不变。
    CompletedSweepBundle(const CompletedSweepBundle& other) = default;
    /// 用另一个公开快照值覆盖当前调用者副本，不影响 Store 中的正式事实。
    /// @param other 只读值来源；调用后来源保持不变。
    /// @return 当前调用者副本引用。
    CompletedSweepBundle& operator=(const CompletedSweepBundle& other) = default;
    /// 转移公开快照值；来源随后只可析构或重新赋值。
    /// @param other 值来源；构造完成后来源只可析构或重新赋值。
    CompletedSweepBundle(CompletedSweepBundle&& other) noexcept = default;
    /// 转移赋值公开快照值；目标旧值被覆盖，但不改变 Store 中的正式事实。
    /// @param other 值来源；赋值完成后来源只可析构或重新赋值。
    /// @return 当前调用者副本引用。
    CompletedSweepBundle& operator=(CompletedSweepBundle&& other) noexcept = default;

    /// @return 与本快照同批完成的 OperationId。
    OperationId operation() const noexcept { return operation_; }
    /// @return 正式 A snapshot ID。
    acquisition::CompletedSweepId id() const noexcept { return id_; }
    /// @return 本快照对应的 Logical Sweep ID。
    acquisition::LogicalSweepId logical_sweep_id() const noexcept {
        return logical_sweep_id_;
    }
    /// @return 与 Operation/fence/status/Event 相同的 Store revision。
    std::uint64_t revision() const noexcept { return revision_; }
    /// @return 实际激励轴有效点数。
    std::uint32_t point_count() const noexcept { return point_count_; }
    /// 读取实际激励频率。
    /// @param index 范围必须为 [0, point_count())。
    /// @return 对应频率，单位 Hz。
    double frequency_hz(std::size_t index) const noexcept {
        return axis_hz_[index];
    }
    /// @return 已发布必需接收机观测数量。
    std::uint32_t observation_count() const noexcept { return observation_count_; }
    /// 读取一项正式接收机观测。
    /// @param index 范围必须为 [0, observation_count())。
    /// @return 对应只读观测引用；生命周期不超过当前 bundle。
    const CompletedReceiverObservation& observation(
        std::size_t index) const noexcept {
        return observations_[index];
    }
    /// @return 当前单板纵切固定为 1；保留数组形状供后续多板扩展。
    std::size_t board_evidence_count() const noexcept { return 1U; }
    /// 读取单板 Run 来源证据。
    /// @param index 当前只允许 0。
    /// @return 对应只读证据引用；生命周期不超过当前 bundle。
    const acquisition::BoardRunEvidence& board_evidence(
        std::size_t index) const noexcept {
        (void)index;
        return evidence_;
    }

private:
    friend class InstrumentStore;
    CompletedSweepBundle(
        OperationId operation,
        std::uint64_t revision,
        const acquisition::CandidateCommitLease& candidate) noexcept;

    OperationId operation_{};
    acquisition::CompletedSweepId id_{};
    acquisition::LogicalSweepId logical_sweep_id_{};
    std::uint64_t revision_{0U};
    std::uint32_t point_count_{0U};
    std::array<double, acquisition::kMaximumCompletedSweepPoints> axis_hz_{};
    std::array<
        CompletedReceiverObservation,
        board::kMaximumPreparedObservations> observations_{};
    std::uint32_t observation_count_{0U};
    acquisition::BoardRunEvidence evidence_{};
};

}  // namespace vna::store
