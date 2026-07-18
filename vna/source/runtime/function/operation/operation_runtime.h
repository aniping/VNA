#pragma once

#include "runtime/core/base/result.h"
#include "runtime/core/base/strong_id.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vna::runtime {

/// 运行时工作实例的唯一标识；0 为无效值。
using WorkId = core::StrongId<struct WorkIdTag>;

/// 工作准入或派发失败原因。
enum class RuntimeErrc {
    /// 固定工作槽已经全部被预留、排队或执行中的工作占用。
    ResourceExhausted,
    /// 工作 ID、预留凭证或完成回调注册无效，或凭证不属于当前运行时。
    InvalidPermit
};

/// 运行时接口返回的类型化错误。
struct RuntimeError final {
    RuntimeErrc code{RuntimeErrc::InvalidPermit};
};

/// 工作离开运行时前的最终状态。
enum class RuntimeTerminalKind {
    /// 工作按预期执行完成。
    Completed,
    /// 工作执行过但报告失败。
    Failed
};

/// 一次工作执行的终态结果。
struct RuntimeTerminal final {
    RuntimeTerminalKind kind{RuntimeTerminalKind::Failed};
};

/// 由 OperationRuntime 延迟执行的工作接口。
///
/// @note RuntimeWork 的对象生命周期由调用者管理。从 dispatch() 成功开始，
///       对象必须保持有效，直到对应的完成回调返回。
class RuntimeWork {
public:
    virtual ~RuntimeWork() = default;

    /// 执行工作主体。
    /// @return 工作的最终状态；每次成功派发只调用一次。
    virtual RuntimeTerminal execute() noexcept = 0;
};

/// 接收运行时工作终态的回调接口。
/// @note 回调对象由调用者持有，生命周期要求与 RuntimeWork 相同。
class RuntimeCompletionSink {
public:
    virtual ~RuntimeCompletionSink() = default;

    /// 通知一项工作已经退出运行时。
    /// @param work 已完成工作的 ID。
    /// @param terminal 工作报告的最终状态。
    /// @note 每次成功派发恰好回调一次；当前实现从 run_one() 调用栈内回调。
    virtual void on_runtime_terminal(
        WorkId work,
        RuntimeTerminal terminal) noexcept = 0;
};

/// move-only 的完成回调注册凭证，防止同一注册被重复派发。
class RuntimeCompletionRegistration final {
public:
    /// @param sink 接收终态的对象；不转移 sink 对象本身的所有权。
    explicit RuntimeCompletionRegistration(RuntimeCompletionSink& sink) noexcept
        : sink_(&sink) {}
    RuntimeCompletionRegistration(RuntimeCompletionRegistration&& other) noexcept;
    RuntimeCompletionRegistration& operator=(
        RuntimeCompletionRegistration&& other) noexcept;
    RuntimeCompletionRegistration(const RuntimeCompletionRegistration&) = delete;
    RuntimeCompletionRegistration& operator=(const RuntimeCompletionRegistration&) = delete;

    /// @return 尚未被派发消费时返回 true。
    bool valid() const noexcept { return sink_ != nullptr; }

private:
    friend class OperationRuntime;
    RuntimeCompletionSink* take_sink() noexcept;

    RuntimeCompletionSink* sink_{nullptr};
};

class OperationRuntime;

/// move-only 的运行槽预留凭证。
///
/// 凭证析构或被其他凭证覆盖时，会把尚未派发的槽位归还给原运行时；
/// dispatch() 成功后凭证失效，槽位所有权转移给 OperationRuntime。
class ReservedWorkDispatch final {
public:
    ReservedWorkDispatch(ReservedWorkDispatch&& other) noexcept;
    ReservedWorkDispatch& operator=(ReservedWorkDispatch&& other) noexcept;
    ReservedWorkDispatch(const ReservedWorkDispatch&) = delete;
    ReservedWorkDispatch& operator=(const ReservedWorkDispatch&) = delete;
    ~ReservedWorkDispatch();

    /// @return 凭证仍持有一个有效预留槽时返回 true。
    bool valid() const noexcept { return owner_ != nullptr; }
    /// @return 预留槽绑定的工作 ID；凭证失效后返回无效 ID。
    WorkId work_id() const noexcept { return work_id_; }

private:
    friend class OperationRuntime;
    ReservedWorkDispatch(
        OperationRuntime& owner,
        std::size_t slot,
        std::uint64_t generation,
        WorkId work_id) noexcept;
    void release() noexcept;
    void invalidate() noexcept;

    OperationRuntime* owner_{nullptr};
    std::size_t slot_{0U};
    std::uint64_t generation_{0U};
    WorkId work_id_{};
};

/// 工作成功进入排队状态后的回执。
struct DispatchReceipt final {
    WorkId work{};
};

/// OperationRuntime 当前资源使用情况的只读快照。
struct RuntimeSnapshot final {
    /// 已预留但尚未派发的槽位数。
    std::size_t reserved{0U};
    /// 已派发、等待执行的槽位数。
    std::size_t queued{0U};
    /// 正在执行的槽位数。
    std::size_t running{0U};
    /// 自运行时创建以来完成的工作总数。
    std::uint64_t completed{0U};
};

/// 固定容量、显式泵动的操作运行时。
///
/// 当前实现不创建线程或 RTOS Task。调用者通过 run_one() 驱动队列，
/// 该模型用于先验证有界准入、所有权和非内联派发契约。
class OperationRuntime final {
public:
    static constexpr std::size_t kMaximumSlots = 16U;

    /// 创建运行时。
    /// @param capacity 可同时处于 Reserved/Queued/Running 状态的最大工作数；
    ///        大于 kMaximumSlots 的值会被截断。
    explicit OperationRuntime(std::size_t capacity) noexcept;

    /// 为工作预留一个固定槽位，但不执行工作。
    /// @param work 非 0 的工作 ID。
    /// @return 成功时返回自动归还能力的预留凭证；容量耗尽或 ID 无效时返回错误。
    core::Result<ReservedWorkDispatch, RuntimeError> reserve_work(
        WorkId work) noexcept;

    /// 把已预留的工作加入执行队列。
    /// @param reservation 必须由当前运行时为同一工作签发；成功后被消费。
    /// @param work 要执行的工作对象；调用者必须保证其存活到完成回调结束。
    /// @param completion move-only 完成注册；成功后被消费，回调对象不转移所有权。
    /// @return 成功时返回派发回执；凭证无效或归属错误时返回 InvalidPermit。
    /// @note 本函数只入队，不会内联调用 execute() 或完成回调。
    core::Result<DispatchReceipt, RuntimeError> dispatch(
        ReservedWorkDispatch&& reservation,
        RuntimeWork& work,
        RuntimeCompletionRegistration&& completion) noexcept;

    /// 执行队列中的第一项工作并同步发出终态回调。
    /// @return 执行了一项工作时返回 true；队列为空时返回 false。
    bool run_one() noexcept;

    /// @return 当前槽位计数和累计完成数的一致性快照。
    RuntimeSnapshot inspect() const noexcept;

private:
    friend class ReservedWorkDispatch;

    enum class SlotState {
        Empty,
        Reserved,
        Queued,
        Running
    };

    struct Slot final {
        SlotState state{SlotState::Empty};
        std::uint64_t generation{0U};
        WorkId work_id{};
        RuntimeWork* work{nullptr};
        RuntimeCompletionSink* completion{nullptr};
    };

    void release_reservation(
        std::size_t slot,
        std::uint64_t generation) noexcept;

    std::array<Slot, kMaximumSlots> slots_{};
    std::size_t capacity_{0U};
    std::uint64_t next_generation_{1U};
    std::uint64_t completed_{0U};
};

}  // namespace vna::runtime
