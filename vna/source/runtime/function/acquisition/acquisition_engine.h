#pragma once

#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/acquisition/acquisition_ingress.h"
#include "runtime/function/acquisition/network_observation_builder.h"
#include "runtime/function/acquisition/acquisition_result.h"
#include "runtime/function/operation/operation_runtime.h"
#include "runtime/platform/board/board_port.h"

#include <cstddef>
#include <optional>

namespace vna::acquisition {

/// 由 OperationRuntime 分步驱动的单板 A-only 采集工作。
///
/// 本类拥有首次派发前取得的完整采集资源租约；Board callback 只能把有界事件
/// 存入本对象，不能调用 InstrumentStore。L2 在 Runtime 交付终态后读取失败事实
/// 并完成 Store 提交，再通知本对象终结外部 owner。Board Prepare 移交 drain
/// owner 时，本对象在 Quarantined terminal 后仍由 L2 保活，不能释放该资源组。
class AcquisitionEngine final : public runtime::RuntimeWork,
                                private board::PrepareSink,
                                private board::BoardRunSink {
public:
    /// 建立一个尚未启动的冻结采集工作。
    /// @param execution 目标单板执行面；必须比本对象活得更久，不转移所有权。
    /// @param intent 冻结的紧凑扫描意图；按值保存，不生成实际频率轴。
    /// @param prepare_authorization 在 L2 准入时按同一能力 cut 签发的授权；转移所有权。
    /// @param prepare_call 已预留的 Prepare 调用 ID。
    /// @param run 已预留的 Run 调用 ID。
    /// @param generation 已预留的 Run 代次。
    /// @param snapshot_id 首次派发前预签发、提交成功后公开的 A snapshot ID。
    /// @param logical_sweep_id 本次完整逻辑扫频 ID。
    /// @param work 与 Accepted Operation 绑定的 Runtime WorkId 事实。
    /// @param ingress_capacity 首次派发前为 Board callback 预留的正式 chunk 数。
    /// @param continuation 首次 dispatch 前冻结的有界 Ingress 持续接收证明。
    /// @param delivery 已预留的数据交付凭证及固定回退 Buffer 槽；转移所有权，
    ///        Run 同步拒绝时由 Adapter 原样返还，接受后由 Adapter 保持到终态。
    /// @param drain Runtime 需要两阶段完成时使用的具名 Drain ID。
    /// @param resources 首次派发前取得的全部采集关键资源；转移所有权并保持到
    ///        普通失败终态提交后、Drain 真实资源终态，或 Quarantine 隔离期结束。
    /// @param board_reservation 首次派发前从目标 Adapter 取得的 Prepare/Run
    ///        call、队列和 callback sink 容量；转移所有权并保持到真实终态。
    AcquisitionEngine(
        board::BoardExecutionPort& execution,
        board::SweepIntent intent,
        board::PrepareAuthorization&& prepare_authorization,
        board::PrepareCallId prepare_call,
        board::BoardRunId run,
        board::RunGeneration generation,
        CompletedSweepId snapshot_id,
        LogicalSweepId logical_sweep_id,
        runtime::WorkId work,
        std::size_t ingress_capacity,
        board::AcquisitionContinuationAttestation continuation,
        board::RunDeliveryGrant&& delivery,
        runtime::DrainId drain,
        AcquisitionAdmissionPool::Lease&& resources,
        board::BoardExecutionReservation&& board_reservation) noexcept;

    /// 工作被 Runtime 非 owning 引用期间地址必须稳定，因此禁止移动。
    AcquisitionEngine(AcquisitionEngine&&) = delete;
    /// 工作被 Runtime 非 owning 引用期间地址必须稳定，因此禁止移动赋值。
    AcquisitionEngine& operator=(AcquisitionEngine&&) = delete;
    /// 独占资源 owner 不可复制。
    AcquisitionEngine(const AcquisitionEngine&) = delete;
    /// 独占资源 owner 不可复制赋值。
    AcquisitionEngine& operator=(const AcquisitionEngine&) = delete;

    /// @copydoc runtime::RuntimeWork::start
    runtime::RuntimeWorkStep start(runtime::ExecutionContext& context) noexcept override;
    /// @copydoc runtime::RuntimeWork::resume
    runtime::RuntimeWorkStep resume(runtime::ExecutionContext& context) noexcept override;
    /// @copydoc runtime::RuntimeWork::resume_drain
    runtime::RuntimeDrainStep resume_drain(
        runtime::ExecutionContext& context) noexcept override;

    /// @return 最近形成的类型化失败事实只读引用；仅在 Runtime 返回
    ///         Failed/Draining 后读取，引用生命周期不超过本对象。
    const AcquisitionFailure& failure() const noexcept { return failure_; }

    /// 在 L2 已把失败 Operation/status/fence/Event 原子提交后终结外部 owner。
    /// @return A-only completion owner 与 disabled Preview owner 首次终结时返回 true。
    /// @note 普通失败只调用一次；Drained 时由 resume_drain() 在收到携带清理
    ///       证据的 PrepareFailed 或匹配 Run terminal 后终结。PrepareSucceeded、
    ///       PrepareDraining、Quarantined/CleanupFailed 均不得调用；重复调用返回 false。
    bool finalize_failure_owners() noexcept;

    /// 取得唯一成功终态的 candidate 与 completion owner 聚合。
    /// @return Runtime 报告 Completed 后首次调用返回 move-only success；失败、
    ///         Draining、尚未完成或重复调用返回空。返回对象由 L2 持有到 commit。
    std::optional<AcquisitionSucceeded> take_success() noexcept;

private:
    enum class Phase {
        Ready,
        Preparing,
        Acquiring,
        Draining,
        Terminal
    };

    enum class DrainObligation {
        None,
        PrepareTerminal,
        RunTerminal,
        Quarantine
    };

    void on_terminal(board::PrepareTerminal&& terminal) noexcept override;
    void on_phase(const board::BoardRunPhaseEvent& event) noexcept override;
    board::ChunkIngressDisposition on_chunk(
        board::ReceiverObservationChunk&& chunk) noexcept override;
    void on_terminal(board::BoardRunTerminal&& terminal) noexcept override;
    bool consume_transition_budget(runtime::ExecutionContext& context) noexcept;
    runtime::RuntimeWorkStep fail(
        AcquisitionFailurePhase phase,
        AcquisitionFailureReason reason) noexcept;
    runtime::RuntimeWorkStep fail_board_rejection(
        AcquisitionFailurePhase phase,
        board::BoardError error) noexcept;
    runtime::RuntimeWorkStep fail_board_terminal(
        AcquisitionFailurePhase phase,
        AcquisitionFailureReason reason,
        board::BoardError error) noexcept;
    runtime::RuntimeWorkStep wait_or_drain(
        runtime::ExecutionContext& context,
        AcquisitionFailurePhase phase) noexcept;
    runtime::RuntimeWorkStep drain_contract_violation(
        AcquisitionFailurePhase phase,
        DrainObligation obligation) noexcept;

    board::BoardExecutionPort* execution_{nullptr};
    board::SweepIntent intent_{};
    board::PrepareAuthorization prepare_authorization_;
    board::PrepareCallId prepare_call_{};
    board::BoardRunId run_{};
    board::RunGeneration generation_{};
    CompletedSweepId snapshot_id_{};
    LogicalSweepId logical_sweep_id_{};
    runtime::WorkId work_{};
    AcquisitionIngress ingress_;
    board::AcquisitionContinuationAttestation continuation_{};
    board::RunDeliveryGrant delivery_;
    runtime::DrainId drain_{};
    AcquisitionAdmissionPool::Lease resources_;
    board::BoardExecutionReservation board_reservation_;
    AcquisitionFailure failure_{};
    Phase phase_{Phase::Ready};
    board::PreparedExecutionId prepared_{};
    std::optional<board::PrepareTerminal> prepare_terminal_{};
    std::optional<board::BoardRunTerminal> run_terminal_{};
    std::optional<NetworkObservationBuilder> builder_{};
    std::optional<AcquisitionSucceeded> success_{};
    std::optional<board::BoardPrepareDrainOwner> board_prepare_drain_owner_{};
    bool callback_contract_violation_{false};
    DrainObligation drain_obligation_{DrainObligation::None};
};

}  // namespace vna::acquisition
