#pragma once

#include "runtime/core/base/result.h"
#include "runtime/core/base/strong_id.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vna::runtime {

/// 运行时工作实例的唯一标识；0 为无效值。
using WorkId = core::StrongId<struct WorkIdTag>;
/// 真实工作终态前转入善后流程的唯一标识；0 为无效值。
using DrainId = core::StrongId<struct DrainIdTag>;

/// 工作准入或派发失败原因。
enum class RuntimeErrc {
    /// WorkId 已被当前 Runtime 的活动槽占用。
    DuplicateWorkId,
    /// deadline 或 budget 不是可接受的有限执行上界。
    InvalidExecutionLimits,
    /// 固定工作槽已经全部被预留、排队或执行中的工作占用。
    ResourceExhausted,
    /// WorkId、预留凭证或 completion mailbox 注册无效，或归属不匹配。
    InvalidPermit
};

/// 运行时接口返回的类型化错误。
struct RuntimeError final {
    RuntimeErrc code{RuntimeErrc::InvalidPermit};
};

/// 父工作退出 Running 阶段时的结果种类。
enum class RuntimeTerminalKind {
    /// 工作按预期执行完成。
    Completed,
    /// 工作执行过但报告失败。
    Failed,
    /// 父工作已移交具名 Drain；运行容量仍由该 Drain 占用。
    Draining
};

/// 父工作退出 Running 阶段时交付的结果或 Draining handoff。
struct RuntimeTerminal final {
    RuntimeTerminalKind kind{RuntimeTerminalKind::Failed};
    /// kind 为 Draining 时必须有效；其他终态为无效 ID。
    DrainId drain{};
};

/// 善后流程离开 Runtime 前的资源终态。
enum class RuntimeDrainTerminalKind {
    /// 清理完成，具名资源已经释放。
    Drained,
    /// 清理结束，但具名资源或会话仍被隔离。
    Quarantined,
    /// 清理失败，并保留未释放资源的诊断证据。
    CleanupFailed
};

/// 同一可靠 completion registration 交付的 Drain 资源终态。
struct RuntimeDrainTerminal final {
    /// 终结的 Drain ID；必须与先前 Draining handoff 一致。
    DrainId drain{};
    RuntimeDrainTerminalKind kind{RuntimeDrainTerminalKind::CleanupFailed};
};

/// 可丢弃的运行进度样本。
/// @note 进度只用于观察，不承担工作完成语义；接收方可拒绝或合并样本。
struct RuntimeProgress final {
    /// 已完成的有界工作单元数。
    std::uint32_t completed_units{0U};
    /// 总工作单元数；0 表示当前工作无法给出总量。
    std::uint32_t total_units{0U};
};

/// Runtime 使用的单调时钟能力；生产实现必须来自平台单调时钟。
class RuntimeMonotonicClock {
public:
    virtual ~RuntimeMonotonicClock() = default;

    /// @return 当前单调 tick；tick 只用于比较先后，不代表 wall-clock 时间。
    virtual std::uint64_t now_tick() const noexcept = 0;
};

/// 工作准入时冻结的有限执行上界。
struct ExecutionLimits final {
    /// 单调时钟上的截止 tick；不得取 uint64_t 最大值。
    std::uint64_t deadline_tick{0U};
    /// 工作可消费的最大预算单元；不得为 0 或 uint64_t 最大值。
    std::uint64_t budget_units{0U};
};

/// 接收可丢弃进度样本的能力接口。
class RuntimeProgressSink {
public:
    virtual ~RuntimeProgressSink() = default;

    /// 尝试接收一个进度样本。
    /// @param progress 当前工作报告的有界进度值，不转移所有权。
    /// @return 样本被接收时返回 true；容量不足或主动合并时返回 false。
    /// @note 返回 false 不能影响工作终态的可靠交付。
    virtual bool try_report(const RuntimeProgress& progress) noexcept = 0;
};

/// 只读停止请求能力。
class StopToken final {
public:
    /// @return 已请求当前工作在下一个有界检查点停止时返回 true。
    bool stop_requested() const noexcept { return requested_; }

private:
    friend class OperationRuntime;
    bool requested_{false};
};

/// 单调时钟上的截止点能力。
class MonotonicDeadline final {
public:
    /// @return 当前工作拥有截止点时返回 true。
    bool enabled() const noexcept { return enabled_; }
    /// @return 平台单调时钟 tick；enabled() 为 false 时返回 0。
    std::uint64_t tick() const noexcept { return tick_; }
    /// @return 已到达或超过截止 tick 时返回 true；未启用时返回 false。
    bool expired() const noexcept {
        return enabled_ && clock_->now_tick() >= tick_;
    }

private:
    friend class OperationRuntime;
    bool enabled_{false};
    std::uint64_t tick_{0U};
    const RuntimeMonotonicClock* clock_{nullptr};
};

/// 当前工作独占的有界预算能力。
class BudgetHandle final {
public:
    /// 尝试消费预算。
    /// @param units 要消费的工作单元；0 不改变预算。
    /// @return 余额足够并已扣减时返回 true；不足时不改变余额并返回 false。
    bool try_consume(std::uint64_t units) noexcept;
    /// @return 当前仍可消费的工作单元数。
    std::uint64_t remaining() const noexcept { return remaining_; }

private:
    friend class OperationRuntime;
    std::uint64_t remaining_{0U};
};

/// Runtime 在每次有界工作状态转换期间提供的执行能力集合。
///
/// @note ExecutionContext 对象及其返回的引用只在当前 start()/resume()
///       调用期间有效，工作不得保存其地址。底层能力由 Runtime 持有到真实终态。
class ExecutionContext final {
public:
    /// @return 当前工作的只读停止请求能力。
    const StopToken& stop() const noexcept { return *stop_; }
    /// @return 当前工作的单调截止点能力。
    const MonotonicDeadline& deadline() const noexcept { return *deadline_; }
    /// @return 当前工作的预算能力；生命周期仅覆盖本次状态转换调用。
    BudgetHandle& budget() const noexcept { return *budget_; }
    /// @return 可丢弃进度接收器；进度拒绝不影响可靠 completion。
    RuntimeProgressSink& progress() const noexcept { return *progress_; }

private:
    friend class OperationRuntime;
    ExecutionContext(
        const StopToken& stop,
        const MonotonicDeadline& deadline,
        BudgetHandle& budget,
        RuntimeProgressSink& progress) noexcept
        : stop_(&stop),
          deadline_(&deadline),
          budget_(&budget),
          progress_(&progress) {}

    const StopToken* stop_{nullptr};
    const MonotonicDeadline* deadline_{nullptr};
    BudgetHandle* budget_{nullptr};
    RuntimeProgressSink* progress_{nullptr};
};

/// 一次异步工作状态转换的结果种类。
enum class RuntimeWorkStepKind {
    /// 工作仍拥有运行容量，后续 pump 必须再次调用 resume()。
    Running,
    /// 工作到达真实成功终态。
    Completed,
    /// 工作到达真实失败终态。
    Failed,
    /// 父工作可终结，但资源转入具名 Drain 并继续占用 Runtime 容量。
    Draining
};

/// RuntimeWork 一次 start()/resume() 调用的有类型结果。
class RuntimeWorkStep final {
public:
    /// @return 构造仍在运行的状态转换结果。
    static RuntimeWorkStep running() noexcept {
        return RuntimeWorkStep{RuntimeWorkStepKind::Running};
    }
    /// @return 构造真实成功终态结果。
    static RuntimeWorkStep completed() noexcept {
        return RuntimeWorkStep{RuntimeWorkStepKind::Completed};
    }
    /// @return 构造真实失败终态结果。
    static RuntimeWorkStep failed() noexcept {
        return RuntimeWorkStep{RuntimeWorkStepKind::Failed};
    }
    /// @param drain 接管全部未终结资源的有效 Drain ID。
    /// @return 构造两段完成协议中的 Draining handoff。
    static RuntimeWorkStep draining(DrainId drain) noexcept {
        return RuntimeWorkStep{RuntimeWorkStepKind::Draining, drain};
    }
    /// @return 当前状态转换的结果种类。
    RuntimeWorkStepKind kind() const noexcept { return kind_; }
    /// @return Draining handoff 的 ID；其他结果返回无效 ID。
    DrainId drain() const noexcept { return drain_; }

private:
    explicit RuntimeWorkStep(
        RuntimeWorkStepKind kind,
        DrainId drain = DrainId{}) noexcept
        : kind_(kind), drain_(drain) {}
    RuntimeWorkStepKind kind_{RuntimeWorkStepKind::Failed};
    DrainId drain_{};
};

/// 一次 Drain 状态转换的结果种类。
enum class RuntimeDrainStepKind {
    /// 善后仍在进行，所有容量继续不可复用。
    Running,
    /// 清理完成且资源已经释放。
    Drained,
    /// 善后终结，但资源或会话保持隔离。
    Quarantined,
    /// 善后失败并保留未释放资源证据。
    CleanupFailed
};

/// RuntimeWork::resume_drain() 一次调用的有类型结果。
class RuntimeDrainStep final {
public:
    /// @return 构造仍在善后的结果。
    static RuntimeDrainStep running() noexcept {
        return RuntimeDrainStep{RuntimeDrainStepKind::Running};
    }
    /// @return 构造资源已释放的唯一 Drain terminal。
    static RuntimeDrainStep drained() noexcept {
        return RuntimeDrainStep{RuntimeDrainStepKind::Drained};
    }
    /// @return 构造资源保持隔离的唯一 Drain terminal。
    static RuntimeDrainStep quarantined() noexcept {
        return RuntimeDrainStep{RuntimeDrainStepKind::Quarantined};
    }
    /// @return 构造清理失败的唯一 Drain terminal。
    static RuntimeDrainStep cleanup_failed() noexcept {
        return RuntimeDrainStep{RuntimeDrainStepKind::CleanupFailed};
    }
    /// @return 当前 Drain 状态转换的结果种类。
    RuntimeDrainStepKind kind() const noexcept { return kind_; }

private:
    explicit RuntimeDrainStep(RuntimeDrainStepKind kind) noexcept : kind_(kind) {}
    RuntimeDrainStepKind kind_{RuntimeDrainStepKind::CleanupFailed};
};

/// 由 OperationRuntime 分步驱动的异步工作接口。
///
/// @note RuntimeWork 的对象生命周期由调用者管理。从 dispatch() 成功开始，
///       普通路径必须保持到 on_runtime_terminal() 返回；Draining 路径必须继续
///       保持到 on_runtime_drain_terminal() 返回。
class RuntimeWork {
public:
    virtual ~RuntimeWork() = default;

    /// 启动工作但不等待外部回调或真实终态。
    /// @param context 当前有界状态转换的执行能力；不得保存其地址。
    /// @return Running 保留运行容量；Completed/Failed 写入唯一普通终态；
    ///         Draining 把全部未终结资源移交给有效 DrainId 并继续占用容量。
    /// @note 每次成功派发恰好调用一次，且不会在 dispatch() 调用栈内执行。
    ///       无效 DrainId 会被 Runtime 转换为 Failed，不会创建无主 Drain。
    virtual RuntimeWorkStep start(ExecutionContext& context) noexcept = 0;

    /// 在后续确定性 pump 中推进已启动工作。
    /// @param context 当前有界状态转换的执行能力；不得保存其地址。
    /// @return Running 继续占用容量；Completed/Failed 写入唯一普通终态；
    ///         Draining 把全部未终结资源移交给有效 DrainId 并继续占用容量。
    /// @note 仅在此前 start()/resume() 返回 Running 后调用，次数有调用方驱动。
    ///       无效 DrainId 会被 Runtime 转换为 Failed，不会创建无主 Drain。
    virtual RuntimeWorkStep resume(ExecutionContext& context) noexcept = 0;

    /// 推进已经完成 Draining handoff 的善后工作。
    /// @param context 当前有界状态转换的执行能力；不得保存其地址。
    /// @return Running 保持容量；其他结果产生唯一 Drain terminal 并释放或隔离资源。
    /// @note 只在 start()/resume() 返回有效 Draining handoff 后调用。默认实现
    ///       返回 CleanupFailed，未产生 Draining 的工作无需覆盖。
    virtual RuntimeDrainStep resume_drain(ExecutionContext& context) noexcept;
};

/// 一次调用内即可得到真实终态的旧式工作接口。
class ImmediateRuntimeWork {
public:
    virtual ~ImmediateRuntimeWork() = default;

    /// 执行立即完成的工作。
    /// @return 本次调用的 Completed 或 Failed 真实终态。
    virtual RuntimeTerminal execute() noexcept = 0;
};

/// 把 ImmediateRuntimeWork 显式适配为分步 RuntimeWork。
/// @note 适配器不拥有被包装对象；从 dispatch() 到完成回调返回，被包装对象
///       和适配器都必须保持有效。
class ImmediateRuntimeWorkAdapter final : public RuntimeWork {
public:
    /// @param work 要包装的立即完成工作；不转移所有权。
    explicit ImmediateRuntimeWorkAdapter(ImmediateRuntimeWork& work) noexcept
        : work_(&work) {}

    /// @copydoc RuntimeWork::start
    RuntimeWorkStep start(ExecutionContext& context) noexcept override;
    /// @copydoc RuntimeWork::resume
    RuntimeWorkStep resume(ExecutionContext& context) noexcept override;
    /// @copydoc RuntimeWork::resume_drain
    RuntimeDrainStep resume_drain(ExecutionContext& context) noexcept override;

private:
    ImmediateRuntimeWork* work_{nullptr};
};

/// 在 Control Executor pump 中同步接收 Runtime completion mailbox 的接口。
/// @note Runtime 不保存本对象地址；对象只需覆盖当前 run_one() 调用。
class RuntimeCompletionSink {
public:
    virtual ~RuntimeCompletionSink() = default;

    /// 通知父工作到达普通终态或完成 Draining handoff。
    /// @param work 本次父工作的 ID。
    /// @param terminal 工作报告的真实终态或 Draining handoff。
    /// @note 每次成功派发恰好回调一次；回调由后续 run_one() 从预留 mailbox
    ///       交付，绝不从 dispatch() 调用栈内触发。Draining 时工作对象、
    ///       registration 与容量继续保留到后续 Drain terminal 回调返回。
    virtual void on_runtime_terminal(
        WorkId work,
        RuntimeTerminal terminal) noexcept = 0;

    /// 通知同一工作先前移交的 Drain 已到达唯一资源终态。
    /// @param work 原父工作的 ID。
    /// @param terminal 包含匹配 DrainId 的资源终态。
    /// @note 只在先前收到 Draining handoff 后调用一次；调用前 Runtime 仍占用容量。
    virtual void on_runtime_drain_terminal(
        WorkId work,
        RuntimeDrainTerminal terminal) noexcept = 0;
};

class OperationRuntime;
class ReservedWorkDispatch;
#if defined(VNA_ENABLE_RUNTIME_CONTRACT_TEST_HOOKS)
class OperationRuntimeContractTestAccess;
#endif

/// Runtime 签发的 move-only completion 接收路由能力。
///
/// @note 路由不保存 sink 地址；同一 Runtime 的不同路由不能消费彼此 mailbox。
///       持有者必须保活本能力，直到所有绑定 registration 到达真实终态。
class RuntimeCompletionReceiver final {
public:
    /// 转移接收路由能力。
    /// @param other 路由来源；构造后 other 失效。
    RuntimeCompletionReceiver(RuntimeCompletionReceiver&& other) noexcept;
    RuntimeCompletionReceiver& operator=(RuntimeCompletionReceiver&&) = delete;
    RuntimeCompletionReceiver(const RuntimeCompletionReceiver&) = delete;
    RuntimeCompletionReceiver& operator=(const RuntimeCompletionReceiver&) = delete;

    /// @return 仍绑定签发 Runtime 与非 0 路由 ID 时返回 true。
    bool valid() const noexcept { return owner_ != nullptr && id_ != 0U; }

private:
    friend class OperationRuntime;
    RuntimeCompletionReceiver(
        OperationRuntime& owner,
        std::uint64_t id) noexcept
        : owner_(&owner), id_(id) {}

    OperationRuntime* owner_{nullptr};
    std::uint64_t id_{0U};
};

/// move-only mailbox 注册，绑定 receiver、WorkId、slot 与 generation。
///
/// @note 本类型只能由 OperationRuntime::reserve_work() 创建，并封装在
///       ReservedWorkDispatch 中；调用者不能在 Accepted 之后临时构造注册。
class RuntimeCompletionRegistration final {
public:
    /// 转移 mailbox 注册绑定。
    /// @param other 注册来源；不转移外部对象所有权，构造后 other 失效。
    RuntimeCompletionRegistration(RuntimeCompletionRegistration&& other) noexcept;
    /// 替换当前绑定并转移另一 mailbox 注册。
    /// @param other 注册来源；赋值后 other 失效。
    /// @return 当前对象引用；绑定生命周期随包含它的 ReservedWorkDispatch。
    RuntimeCompletionRegistration& operator=(
        RuntimeCompletionRegistration&& other) noexcept;
    RuntimeCompletionRegistration(const RuntimeCompletionRegistration&) = delete;
    RuntimeCompletionRegistration& operator=(const RuntimeCompletionRegistration&) = delete;

    /// @return 尚未被派发消费时返回 true。
    bool valid() const noexcept { return owner_ != nullptr; }
    /// @return 注册绑定的工作 ID；凭证失效后返回无效 ID。
    WorkId work_id() const noexcept { return work_id_; }

private:
    friend class OperationRuntime;
    friend class ReservedWorkDispatch;
    RuntimeCompletionRegistration() noexcept = default;
    RuntimeCompletionRegistration(
        OperationRuntime& owner,
        std::size_t slot,
        std::uint64_t generation,
        WorkId work,
        std::uint64_t receiver_id) noexcept;
    bool consume(
        OperationRuntime& owner,
        std::size_t slot,
        std::uint64_t generation,
        WorkId work,
        std::uint64_t receiver_id) noexcept;

    OperationRuntime* owner_{nullptr};
    std::size_t slot_{0U};
    std::uint64_t generation_{0U};
    WorkId work_id_{};
    std::uint64_t receiver_id_{0U};
};

/// move-only 的运行槽预留凭证。
///
/// 凭证析构或被其他凭证覆盖时，会把尚未派发的槽位归还给原运行时；
/// dispatch() 成功后凭证失效，槽位所有权转移给 OperationRuntime。
class ReservedWorkDispatch final {
public:
    /// 转移执行 permit 与绑定的 completion registration。
    /// @param other 凭证来源；构造后 other 失效且不再归还槽位。
    ReservedWorkDispatch(ReservedWorkDispatch&& other) noexcept;
    /// 归还当前尚未派发的槽位，再转移另一完整预留。
    /// @param other 凭证来源；赋值后 other 失效。
    /// @return 当前对象引用；新预留继续由本对象 RAII 管理。
    ReservedWorkDispatch& operator=(ReservedWorkDispatch&& other) noexcept;
    ReservedWorkDispatch(const ReservedWorkDispatch&) = delete;
    ReservedWorkDispatch& operator=(const ReservedWorkDispatch&) = delete;
    /// 析构尚未派发的预留，并同步归还执行与 completion 容量。
    ~ReservedWorkDispatch();

    /// @return 凭证仍持有一个有效预留槽时返回 true。
    bool valid() const noexcept { return owner_ != nullptr; }
    /// @return 预留槽绑定的工作 ID；凭证失效后返回无效 ID。
    WorkId work_id() const noexcept { return work_id_; }
    /// @return 已在 Accepted 之前绑定可靠 completion slot 时返回 true。
    bool completion_reserved() const noexcept { return completion_.valid(); }

private:
    friend class OperationRuntime;
    ReservedWorkDispatch(
        OperationRuntime& owner,
        std::size_t slot,
        std::uint64_t generation,
        WorkId work_id,
        RuntimeCompletionRegistration&& completion) noexcept;
    void release() noexcept;
    void invalidate() noexcept;

    OperationRuntime* owner_{nullptr};
    std::size_t slot_{0U};
    std::uint64_t generation_{0U};
    WorkId work_id_{};
    RuntimeCompletionRegistration completion_{};
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
    /// 正在执行或等待普通 completion 交付的槽位数。
    std::size_t running{0U};
    /// 已完成父工作 handoff、正由 Drain 执行或等待资源终态交付的槽位数。
    std::size_t draining{0U};
    /// 自运行时创建以来完成的工作总数。
    std::uint64_t completed{0U};
};

/// 固定容量、显式泵动的操作运行时。
///
/// 当前实现不创建线程或 RTOS Task。调用者通过 run_one() 驱动队列与
/// completion mailbox，该模型用于验证有界准入、所有权和非内联派发契约。
class OperationRuntime final {
public:
    static constexpr std::size_t kMaximumSlots = 16U;

    /// 创建运行时。
    /// @param capacity 可同时处于 Reserved/Queued/Running/Draining 状态的
    ///        最大工作数；大于 kMaximumSlots 的值会被截断。
    /// @param clock 平台单调时钟；不转移所有权，必须比 Runtime 活得更久。
    OperationRuntime(
        std::size_t capacity,
        RuntimeMonotonicClock& clock) noexcept;

    /// 签发一个与其他控制消费者隔离的 completion 接收路由。
    /// @return 当前 Runtime 拥有的 move-only 路由能力；不分配堆内存且不失败。
    /// @note 路由必须在首次 reserve_work() 前取得，并保活到全部绑定终态交付。
    RuntimeCompletionReceiver register_completion_receiver() noexcept;

    /// 为工作同时预留固定执行槽和可靠 completion registration，但不执行工作。
    /// @param work 非 0 的工作 ID。
    /// @param limits deadline 与 budget 的有限上界，按值冻结到该工作槽。
    /// @param receiver 当前 Runtime 签发的接收路由；不转移所有权，registration
    ///        会绑定其稳定 ID，其他路由不能消费该工作的 completion。
    /// @return 成功时返回同时拥有执行 permit 与 completion registration 的
    ///         自动归还凭证；ID/limits 无效、ID 重复或容量耗尽时返回错误。
    core::Result<ReservedWorkDispatch, RuntimeError> reserve_work(
        WorkId work,
        ExecutionLimits limits,
        const RuntimeCompletionReceiver& receiver) noexcept;

    /// 把已预留的工作加入执行队列。
    /// @param reservation 必须由当前运行时为同一工作签发；成功后被消费。
    /// @param work 要执行的工作对象；普通路径必须存活到父工作终态回调结束，
    ///        Draining 路径必须继续存活到 Drain 终态回调结束；不转移所有权。
    /// @return 成功时返回派发回执；执行 permit 或内含 completion registration
    ///         无效、归属错误或绑定不一致时返回 InvalidPermit。
    /// @note 本函数只入队，不会内联调用 start()/resume() 或完成回调。
    core::Result<DispatchReceipt, RuntimeError> dispatch(
        ReservedWorkDispatch&& reservation,
        RuntimeWork& work) noexcept;

    /// 请求活动工作在下一个有界状态转换观察到 stop。
    /// @param work 要停止的活动 WorkId。
    /// @return 找到匹配 Reserved/Queued/Running/Draining 工作并记录请求时返回
    ///         true；ID 无效或工作已经释放时返回 false。
    bool request_stop(WorkId work) noexcept;

    /// 泵动一个 completion mailbox 或 Queued/Running/Draining 状态转换。
    /// @param receiver 当前控制消费者持有的路由能力；不转移或保存所有权。
    /// @param completion 当前 Control Executor 的内部终态接收器；不转移或保存
    ///        所有权，回调只发生在本次调用栈内，不能使用协议会话对象。
    /// @return 交付或推进了一项工作时返回 true；没有可泵动状态时返回 false。
    /// @note 工作终态先进入预留 mailbox，下一次 pump 才交付。Running、待交付
    ///       completion 与 Draining 都保留槽位，terminal 回调返回后才释放容量；
    ///       completion 回调内重入本函数返回 false，不推进任何其他工作。
    bool run_one(
        const RuntimeCompletionReceiver& receiver,
        RuntimeCompletionSink& completion) noexcept;

    /// @return 当前槽位计数和累计完成数的一致性快照。
    RuntimeSnapshot inspect() const noexcept;

private:
    friend class ReservedWorkDispatch;
#if defined(VNA_ENABLE_RUNTIME_CONTRACT_TEST_HOOKS)
    friend class OperationRuntimeContractTestAccess;
#endif

    enum class SlotState {
        Empty,
        Reserved,
        Queued,
        Running,
        Draining,
        WorkTerminalReady,
        DrainTerminalReady,
        DeliveringWorkTerminal,
        DeliveringDrainHandoff,
        DeliveringDrainTerminal
    };

    struct Slot final {
        SlotState state{SlotState::Empty};
        std::uint64_t generation{0U};
        WorkId work_id{};
        std::uint64_t receiver_id{0U};
        RuntimeWork* work{nullptr};
        DrainId drain{};
        RuntimeTerminal pending_work_terminal{};
        RuntimeDrainTerminal pending_drain_terminal{};
        StopToken stop{};
        MonotonicDeadline deadline{};
        BudgetHandle budget{};
    };

    class DiscardingProgressSink final : public RuntimeProgressSink {
    public:
        bool try_report(const RuntimeProgress& progress) noexcept override {
            (void)progress;
            return false;
        }
    };

    void release_reservation(
        std::size_t slot,
        std::uint64_t generation) noexcept;
    void handle_work_step(Slot& slot, RuntimeWorkStep step) noexcept;
    void handle_drain_step(Slot& slot, RuntimeDrainStep step) noexcept;

    std::array<Slot, kMaximumSlots> slots_{};
    std::size_t capacity_{0U};
    std::uint64_t next_generation_{1U};
    std::uint64_t next_receiver_id_{1U};
    std::uint64_t completed_{0U};
    DiscardingProgressSink progress_{};
    RuntimeMonotonicClock* clock_{nullptr};
    bool pumping_{false};
#if defined(VNA_ENABLE_RUNTIME_CONTRACT_TEST_HOOKS)
    /// 只允许 friend 测试访问器触发；产品接口无法设置，消费一次后自动清除。
    bool reject_next_dispatch_for_contract_test_{false};
#endif
};

}  // namespace vna::runtime
