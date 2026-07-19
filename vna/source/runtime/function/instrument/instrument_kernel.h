#pragma once

#include "runtime/core/base/result.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/acquisition/acquisition_engine.h"
#include "runtime/function/operation/operation_runtime.h"
#include "runtime/platform/board/acquisition_buffer_pool.h"
#include "runtime/platform/board/board_port.h"
#include "runtime/resource/store/instrument_store.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vna::instrument {

/// 显式允许开发环境发布 A 阶段、但禁止冒充普通 Channel Sweep 的授权值。
struct AOnlyDiagnosticAuthorization final {
    /// 建立无授权值；提交时会被类型化拒绝。
    AOnlyDiagnosticAuthorization() noexcept = default;
    /// @return 仅用于 Mock 开发组合的显式 raw/diagnostic 授权。
    static AOnlyDiagnosticAuthorization issue_for_mock_diagnostics() noexcept {
        return AOnlyDiagnosticAuthorization{true};
    }
    /// @return 调用者明确选择 Mock raw/diagnostic purpose 时为 true。
    bool valid() const noexcept { return granted; }

private:
    explicit AOnlyDiagnosticAuthorization(bool value) noexcept : granted(value) {}
    bool granted{false};
};

/// 公共 L2 A-only 扫描请求；只携带紧凑元数据和显式授权。
struct AOnlySweepRequest final {
    /// 请求点数；范围由当前 Board capability 冻结验证。
    std::uint32_t point_count{0U};
    /// 请求起始频率，单位 Hz。
    double start_hz{0.0};
    /// 请求终止频率，单位 Hz。
    double stop_hz{0.0};
    /// 仅允许 Mock raw/diagnostic A-only purpose 的授权。
    AOnlyDiagnosticAuthorization authorization{};
    /// 可选的 Board capability revision 前置条件；0 表示由 Kernel 冻结当前
    /// revision，非 0 值必须与提交时读取的 capability cut 精确一致。
    std::uint64_t expected_capability_revision{0U};
};

/// A-only 公共提交入口的有限执行配置。
struct AOnlyKernelProfile final {
    /// 从提交时单调 tick 起算的 Runtime deadline 距离；必须大于 0。
    std::uint64_t deadline_span_ticks{0U};
    /// 每项 Acquisition 可消费的 Runtime 预算；必须大于 0。
    std::uint64_t budget_units{0U};
    /// 从提交时 Board 单调 tick 起算的 Ingress attestation 有限跨度；必须大于 0。
    std::uint64_t board_continuation_span_ticks{1000000U};
};

/// A-only 提交被拒绝的稳定分类。
enum class AOnlySubmitErrc {
    /// 请求没有显式 Mock raw/diagnostic 授权。
    DiagnosticAuthorizationRequired,
    /// 点数、频率或有限执行 Profile 非法。
    InvalidRequest,
    /// 请求指定的 capability revision 与当前 Board cut 不一致或当前 cut 无效。
    RevisionConflict,
    /// Kernel 固定 Operation/Work 映射槽已满。
    ControllerCapacityExhausted,
    /// A/candidate/Buffer/Ingress/Board owner 聚合资源槽不足。
    AcquisitionResourcesUnavailable,
    /// Runtime 无法预留工作、预算、deadline 或可靠 completion。
    RuntimeAdmissionRejected,
    /// Store 无法预留 terminal/status/fence/Event 生命周期容量。
    StoreAdmissionRejected,
    /// Store 拒绝把完整预留安装为 Accepted，例如 OperationId 冲突。
    StoreInitialCommitRejected,
    /// Kernel 已检测到 Store 终态预留不变量失效，禁止接受新的仪器操作。
    InstrumentFailStop
};

/// A-only 提交拒绝结果。
struct AOnlySubmitError final {
    /// 稳定拒绝分类；调用者不解析日志文本。
    AOnlySubmitErrc code{AOnlySubmitErrc::InvalidRequest};
    /// 同步提交拒绝固定发生在首次 Accepted/dispatch 前的 Admission 阶段。
    acquisition::AcquisitionFailurePhase phase{
        acquisition::AcquisitionFailurePhase::Admission};
    /// 调用者再次提交前必须满足的稳定前置条件。
    acquisition::AcquisitionRetryClass retry{
        acquisition::AcquisitionRetryClass::DoNotRetryWithoutChange};
    /// Run 尚未被接受的执行生命周期事实；不代表物理 RF 安全证明。
    acquisition::AcquisitionSafetyImpact safety{
        acquisition::AcquisitionSafetyImpact::NoRunAccepted};
    /// 已读取 capability cut 时保存 BoardSessionId；授权前拒绝时无效。
    board::BoardSessionId board_session{};
    /// 已读取 capability cut 时保存其 revision；授权前拒绝时为 0。
    std::uint64_t capability_revision{0U};
    /// 原请求携带的 revision 前置条件；0 表示未显式指定。
    std::uint64_t expected_capability_revision{0U};
};

/// A-only 提交成功回执；不暴露 Runtime、Board 或输出 Buffer 身份。
struct AcceptedAOnlyOperation final {
    /// 已在 Store 可查询、但不代表采集完成的 OperationId。
    store::OperationId operation{};
};

/// A-only 公共提交的 Accepted/Rejected 封闭结果。
using AOnlySubmitResult = core::Result<AcceptedAOnlyOperation, AOnlySubmitError>;

/// InstrumentKernel 是否仍允许接受新业务操作。
enum class InstrumentIntegrityState {
    /// Store 终态提交不变量保持成立。
    Healthy,
    /// 已安装的 Store 终态预留无法提交，只能由尚未实现的恢复流程解除。
    StoreFailStop
};

/// Kernel 完整性状态的只读类型化快照。
struct InstrumentIntegritySnapshot final {
    /// 当前是否处于 Store fail-stop。
    InstrumentIntegrityState state{InstrumentIntegrityState::Healthy};
    /// 触发 fail-stop 的 Operation；Healthy 时无效。
    store::OperationId operation{};
    /// Store 是否返回了可保存的类型化错误。
    bool has_store_error{false};
    /// has_store_error 为 true 时的 Store 错误；只用于诊断，不作为恢复能力。
    store::StoreError store_error{};
};

/// Web/SCPI 之下共享的最小 L2 仪器业务入口。
///
/// 当前只开放显式 Mock diagnostic A-only tracer bullet。普通 Channel Sweep 没有
/// 复用此请求类型或授权的入口，Real Board 产品组合也尚未提供。
/// @note 本对象及构造参数必须覆盖全部已接受 Operation 的普通/Drain 终态及
///       Quarantined owner 的整个隔离期；析构不是取消、Board abort 或 RF-safe 命令。
class InstrumentKernel final : private runtime::RuntimeCompletionSink {
public:
    /// Kernel 内同时保活的 A-only Operation 上限。
    static constexpr std::size_t kMaximumAOnlyOperations = 16U;

    /// @param runtime 固定容量 OperationRuntime；必须比本对象活得更久。
    /// @param store 权威 Operation/Event Store；必须比本对象活得更久。
    /// @param board_execution 已打开 Mock 产品组合的 Board execution seam；不转移所有权。
    /// @param acquisition_resources 首次派发前取得关键采集容量的池；必须比本对象活得更久。
    /// @param clock 与 Runtime 相同时间域的单调时钟；用于冻结每项 deadline。
    /// @param profile 有限 deadline 距离和预算；构造后按值冻结。
    InstrumentKernel(
        runtime::OperationRuntime& runtime,
        store::InstrumentStore& store,
        board::BoardExecutionPort& board_execution,
        acquisition::AcquisitionAdmissionPool& acquisition_resources,
        runtime::RuntimeMonotonicClock& clock,
        AOnlyKernelProfile profile) noexcept;

    /// 提交一次明确授权的单板 A-only raw/diagnostic 扫描。
    /// @param request 只读紧凑请求；不含 RuntimeWork、Board token 或输出数组，
    ///        调用返回后无需继续保活。
    /// @return Accepted 只携带可查询 OperationId；Rejected 携带类型化原因且不创建
    ///         Operation/Event/Board 调用；非 0 expected_capability_revision 不匹配
    ///         时返回 RevisionConflict。调用不等待 Prepare、Run 或终态。
    AOnlySubmitResult submit_a_only(const AOnlySweepRequest& request) noexcept;

    /// 推进一项 Runtime 状态转换或交付一条可靠 completion mailbox 消息。
    /// @return 有工作被推进时返回 true；当前 receiver 无待办时返回 false。
    bool run_one() noexcept;

    /// 查询是否因 Store 终态不变量失效而停止接受新操作。
    /// @return 值快照；不暴露 candidate、owner 或可执行恢复能力。
    InstrumentIntegritySnapshot inspect_integrity() const noexcept {
        return integrity_;
    }

private:
    /// A-only 最多包含 a/b 两项 201 点观测；Claim、Pool 与 Ingress 共用此上界。
    static constexpr std::size_t kAOnlyChunkCapacity =
        2U * board::kMaximumChunksPerObservation;

    struct Slot final {
        bool active{false};
        bool release_pending{false};
        runtime::WorkId work{};
        store::OperationId operation{};
        std::optional<acquisition::AcquisitionEngine> engine{};
        /// Store success commit 后 owner 无法安全终结时继续隔离的聚合。
        std::optional<acquisition::AcquisitionSucceeded> pending_success{};
    };

    void on_runtime_terminal(
        runtime::WorkId work,
        runtime::RuntimeTerminal terminal) noexcept override;
    void on_runtime_drain_terminal(
        runtime::WorkId work,
        runtime::RuntimeDrainTerminal terminal) noexcept override;
    std::size_t find_free_slot() const noexcept;
    std::size_t find_slot(runtime::WorkId work) const noexcept;
    bool accept_state_only_failure_commit(
        store::OperationId operation,
        const core::Result<
            store::TerminalCommitReceipt,
            store::StoreError>& result) noexcept;
    void release_completed_slots() noexcept;

    runtime::OperationRuntime& runtime_;
    store::InstrumentStore& store_;
    board::BoardExecutionPort& board_execution_;
    acquisition::AcquisitionAdmissionPool& acquisition_resources_;
    runtime::RuntimeMonotonicClock& clock_;
    AOnlyKernelProfile profile_{};
    runtime::RuntimeCompletionReceiver completion_receiver_;
    /// 单板一次只允许一项 execution，A-only 为 a/b 的最坏分块数原子预留槽位。
    board::AcquisitionBufferPool acquisition_buffers_{kAOnlyChunkCapacity};
    std::array<Slot, kMaximumAOnlyOperations> slots_{};
    std::uint64_t next_operation_id_{1U};
    std::uint64_t next_work_id_{1U};
    std::uint64_t next_completed_sweep_id_{1U};
    std::uint64_t next_logical_sweep_id_{1U};
    InstrumentIntegritySnapshot integrity_{};
};

}  // namespace vna::instrument
