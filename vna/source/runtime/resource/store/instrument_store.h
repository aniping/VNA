#pragma once

#include "runtime/core/base/result.h"
#include "runtime/core/base/strong_id.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace vna::store {

/// 对外可见操作的唯一标识；0 为无效值。
using OperationId = core::StrongId<struct OperationIdTag>;

/// 仪器状态存储操作的失败原因。
enum class StoreErrc {
    /// 固定生命周期槽位全部被占用。
    ResourceExhausted,
    /// 预留凭证无效、已消费或不属于当前 Store。
    InvalidReservation,
    /// 操作 ID 无效，或把 Accepted 当作终态提交。
    InvalidOperation,
    /// 相同 OperationId 已经可见。
    DuplicateOperation,
    /// 找不到要提交终态的操作。
    OperationNotFound
};

/// Store 接口返回的类型化错误。
struct StoreError final {
    StoreErrc code{StoreErrc::InvalidReservation};
};

/// 对外可查询的操作生命周期状态。
enum class OperationState {
    /// 操作已经可靠接受，后续必须能够落下一个终态。
    Accepted,
    /// 操作成功完成。
    Completed,
    /// 操作失败结束。
    Failed
};

/// 某次查询时刻的操作状态副本。
struct OperationSnapshot final {
    OperationId id{};
    OperationState state{OperationState::Accepted};
    /// Store 全局单调递增修订号，用于判断快照先后关系。
    std::uint64_t revision{0U};
};

class InstrumentStore;

/// move-only 的生命周期终态容量预留。
///
/// 其目的是在对外提交 Accepted 之前，先保证该操作将来一定有容量提交终态。
/// 未经 commit_accepted() 消费的凭证会在析构时自动归还槽位。
class LifecycleTerminalReservation final {
public:
    LifecycleTerminalReservation(LifecycleTerminalReservation&& other) noexcept;
    LifecycleTerminalReservation& operator=(
        LifecycleTerminalReservation&& other) noexcept;
    LifecycleTerminalReservation(const LifecycleTerminalReservation&) = delete;
    LifecycleTerminalReservation& operator=(const LifecycleTerminalReservation&) = delete;
    ~LifecycleTerminalReservation();

    /// @return 凭证仍持有 Store 槽位时返回 true。
    bool valid() const noexcept { return owner_ != nullptr; }

private:
    friend class InstrumentStore;
    LifecycleTerminalReservation(
        InstrumentStore& owner,
        std::size_t slot,
        std::uint64_t generation) noexcept;
    void release() noexcept;
    void invalidate() noexcept;

    InstrumentStore* owner_{nullptr};
    std::size_t slot_{0U};
    std::uint64_t generation_{0U};
};

/// Accepted 状态成功提交后的回执。
struct AcceptedCommitReceipt final {
    OperationId operation{};
    std::uint64_t revision{0U};
};

/// Accepted 提交被拒绝时的结果。
struct RejectedAcceptedCommit final {
    StoreError error{};
    /// 原样返还的终态容量凭证，调用者可决定重试或让其析构归还容量。
    LifecycleTerminalReservation reclaimed;
};

using AcceptedCommitResult = std::variant<
    AcceptedCommitReceipt,
    RejectedAcceptedCommit>;

/// 终态提交是否改变了 Store。
enum class TerminalCommitDisposition {
    /// 本次调用首次写入终态。
    Committed,
    /// 操作此前已经是终态，本次调用保持原状态和修订号。
    AlreadyTerminal
};

/// 终态提交成功后的回执。
struct TerminalCommitReceipt final {
    OperationId operation{};
    OperationState state{OperationState::Failed};
    std::uint64_t revision{0U};
    TerminalCommitDisposition disposition{TerminalCommitDisposition::Committed};
};

/// Store 资源占用和修订号的只读快照。
struct StoreSnapshot final {
    /// 已预留但尚未公开为 Accepted 的生命周期数。
    std::size_t reserved_lifecycles{0U};
    /// 当前可以按 OperationId 查询的操作数。
    std::size_t visible_operations{0U};
    /// 最近一次状态变更后的全局修订号。
    std::uint64_t revision{0U};
};

/// 固定容量的仪器操作生命周期存储。
///
/// 当前仅保存操作生命周期，不保存通道、迹线或测量数据。所有接口均为同步调用，
/// 且不在内部动态扩容，便于后续映射到 RTOS 的有界资源模型。
class InstrumentStore final {
public:
    static constexpr std::size_t kMaximumOperations = 16U;

    /// @param capacity 可见操作与预留生命周期合计上限；超过
    ///        kMaximumOperations 的值会被截断。
    explicit InstrumentStore(std::size_t capacity) noexcept;

    /// 在公开 Accepted 之前预留该操作未来的终态存储容量。
    /// @return 成功时返回自动归还能力的 move-only 凭证；无空槽时返回 ResourceExhausted。
    core::Result<LifecycleTerminalReservation, StoreError>
    reserve_lifecycle_terminal() noexcept;

    /// 原子地把预留槽转为对外可见的 Accepted 操作。
    /// @param operation 非 0 且尚未存在的操作 ID。
    /// @param reservation 当前 Store 签发的有效凭证；成功时被消费。
    /// @return 成功时返回修订号；失败时返回错误并原样返还 reservation。
    AcceptedCommitResult commit_accepted(
        OperationId operation,
        LifecycleTerminalReservation&& reservation) noexcept;

    /// 为已接受操作写入最终状态。
    /// @param operation 已经可见的操作 ID。
    /// @param terminal_state 必须为 Completed 或 Failed。
    /// @return 首次提交时更新状态和修订号；重复提交返回 AlreadyTerminal；
    ///         操作不存在或状态参数非法时返回错误。
    core::Result<TerminalCommitReceipt, StoreError> commit_terminal(
        OperationId operation,
        OperationState terminal_state) noexcept;

    /// 按 ID 查询操作状态。
    /// @param operation 要查询的操作 ID。
    /// @return 找到时返回值拷贝；不存在时返回 std::nullopt。
    std::optional<OperationSnapshot> inspect_operation(
        OperationId operation) const noexcept;

    /// @return 当前容量使用和全局修订号快照。
    StoreSnapshot inspect() const noexcept;

private:
    friend class LifecycleTerminalReservation;

    enum class SlotState {
        Empty,
        Reserved,
        Visible
    };

    struct Slot final {
        SlotState slot_state{SlotState::Empty};
        std::uint64_t generation{0U};
        OperationSnapshot operation{};
    };

    void release_reservation(
        std::size_t slot,
        std::uint64_t generation) noexcept;

    std::array<Slot, kMaximumOperations> slots_{};
    std::size_t capacity_{0U};
    std::uint64_t next_generation_{1U};
    std::uint64_t revision_{0U};
};

}  // namespace vna::store
