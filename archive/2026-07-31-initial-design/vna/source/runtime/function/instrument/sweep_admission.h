#pragma once

#include "runtime/function/operation/operation_runtime.h"
#include "runtime/resource/store/instrument_store.h"

#include <array>
#include <cstddef>

namespace vna::instrument {

/// 扫描操作在进入运行时前可能出现的拒绝原因。
enum class SweepAdmissionErrc {
    /// WorkId 已被 Running 或 Draining 扫描占用，不能建立第二条活动映射。
    DuplicateWorkId,
    /// 控制器没有空闲的 WorkId 到 OperationId 映射槽。
    ControllerCapacityExhausted,
    /// OperationRuntime 无法预留工作容量。
    RuntimeAdmissionRejected,
    /// InstrumentStore 无法预留生命周期终态容量。
    StoreAdmissionRejected,
    /// Accepted 初始状态提交失败，例如操作 ID 无效或重复。
    StoreInitialCommitRejected,
    /// Accepted 已提交，但运行时拒绝了本应有效的派发凭证。
    RuntimeDispatchContractViolation
};

/// 扫描准入失败的类型化错误。
struct SweepAdmissionError final {
    SweepAdmissionErrc code{SweepAdmissionErrc::ControllerCapacityExhausted};
};

/// 扫描 Accepted 状态已经对外可见后的回执。
/// @note 正常情况下工作已进入运行时队列；若 Accepted 之后发生内部派发
///       契约异常，操作可能已经立即转为 Failed。
struct AcceptedSweepOperation final {
    store::OperationId operation{};
    runtime::WorkId work{};
};

/// 接收扫描操作最终可见状态的回调接口。
/// @note 回调对象由调用者持有，必须存活到对应操作完成回调返回。
class SweepCompletionSink {
public:
    virtual ~SweepCompletionSink() = default;

    /// 通知扫描的终态已经成功写入 InstrumentStore。
    /// @param operation Store 中最新的 Completed 或 Failed 快照。
    /// @note 每次成功派发的扫描最多调用一次；Store 提交失败时不会调用。
    virtual void on_sweep_terminal(
        store::OperationSnapshot operation) noexcept = 0;
};

/// 协调运行时容量预留、生命周期提交和工作派发的扫描准入控制器。
///
/// 成功路径严格遵守：预留 Runtime -> 预留 Store 终态 -> 提交 Accepted
/// -> 派发工作。这样调用者一旦看见 Accepted，系统就已经为最终状态留出容量。
class SweepAdmissionController final : private runtime::RuntimeCompletionSink {
public:
    static constexpr std::size_t kMaximumMappings = 16U;

    /// @param runtime 接收已准入工作的有界运行时；必须比本控制器活得更久。
    /// @param store 保存操作生命周期的 Store；必须比本控制器活得更久。
    SweepAdmissionController(
        runtime::OperationRuntime& runtime,
        store::InstrumentStore& store) noexcept;

    /// 尝试接受并派发一项扫描工作。
    /// @param operation 对外暴露的非 0 操作 ID，不能与 Store 中已有 ID 重复。
    /// @param work_id 运行时内部使用的非 0 工作 ID；活动映射中必须唯一。
    /// @param limits 本次扫描冻结的有限 deadline 与 budget 上界。
    /// @param work 实际工作对象；普通路径成功后必须存活到 completion 回调结束；
    ///        Draining 路径必须继续存活到 Runtime 的 Drain 终态回调结束；
    ///        不转移所有权。
    /// @param completion 扫描终态接收器；成功后必须存活到回调结束。
    /// @return 两项预留、Accepted 提交和派发均成功时返回操作回执；任一预留
    ///         或初始提交失败时返回对应错误，且不会执行 work。若 Accepted 已
    ///         可见后发生内部派发契约异常，操作会立即落为 Failed，但本函数仍
    ///         返回已接受回执，因为此时不能再声称该操作从未被接受。
    core::Result<AcceptedSweepOperation, SweepAdmissionError> submit(
        store::OperationId operation,
        runtime::WorkId work_id,
        runtime::ExecutionLimits limits,
        runtime::RuntimeWork& work,
        SweepCompletionSink& completion) noexcept;

    /// 推进一个 Runtime 状态转换或交付一条预留 completion mailbox 消息。
    /// @return 本次调用推进或交付了工作时返回 true；当前无工作时返回 false。
    /// @note 回调只在本调用栈内进入控制器；Runtime 不保存控制器地址。
    bool run_one() noexcept;

private:
    struct Mapping final {
        bool active{false};
        runtime::WorkId work{};
        store::OperationId operation{};
        SweepCompletionSink* completion{nullptr};
        runtime::DrainId drain{};
    };

    void on_runtime_terminal(
        runtime::WorkId work,
        runtime::RuntimeTerminal terminal) noexcept override;
    /// 接收 Runtime 的唯一 Drain 资源终态并释放保留的 WorkId 映射。
    /// @param work 原扫描工作的 ID。
    /// @param terminal 与先前 Draining handoff 匹配的资源终态。
    void on_runtime_drain_terminal(
        runtime::WorkId work,
        runtime::RuntimeDrainTerminal terminal) noexcept override;
    std::size_t find_free_mapping() const noexcept;
    std::size_t find_mapping(runtime::WorkId work) const noexcept;

    runtime::OperationRuntime& runtime_;
    store::InstrumentStore& store_;
    runtime::RuntimeCompletionReceiver completion_receiver_;
    std::array<Mapping, kMaximumMappings> mappings_{};
};

}  // namespace vna::instrument
