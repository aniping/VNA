#pragma once

#include "runtime/platform/board/board_port.h"

namespace vna::acquisition {

/// A-only 采集失败发生的稳定阶段。
enum class AcquisitionFailurePhase {
    /// 已进入 Runtime，但尚未成功提交任何 Board 调用。
    Admission,
    /// Board Prepare 提交或异步终态阶段。
    Prepare,
    /// 使用实际 Manifest 收窄预留资源的阶段。
    ManifestFinalization,
    /// Board Run 提交或异步终态阶段。
    Run,
    /// Accepted 后发现 Runtime 内部派发契约不成立。
    RuntimeDispatch
};

/// A-only 采集失败的稳定原因。
enum class AcquisitionFailureReason {
    /// ExecutionContext 已请求停止。
    StopRequested,
    /// ExecutionContext 的有限 deadline 已到期。
    DeadlineExpired,
    /// 本次状态转换没有足够的预留预算。
    BudgetExhausted,
    /// L2 交付的关键资源租约或 Board 身份不完整。
    InvalidAdmissionResources,
    /// Board 同步拒绝了 Prepare 或 Run。
    BoardRejected,
    /// Board 通过唯一异步终态报告 Run 失败。
    BoardTerminalFailed,
    /// Board Prepare 已移交仍在排空的资源 owner，执行容量必须隔离保留。
    BoardPrepareDraining,
    /// Prepared Manifest 超出准入时冻结的保守资源范围。
    ManifestOutsideAdmission,
    /// Board 回调的身份、顺序或数量违反公开契约。
    BoardContractViolation,
    /// 当前里程碑尚未允许成功数据候选进入 Store。
    SuccessPathUnavailable,
    /// Accepted 后 Runtime 拒绝了此前签发的有效派发能力。
    RuntimeDispatchContractViolation
};

/// 可由 L4 交付并由 L5 原样保存的类型化采集失败事实。
struct AcquisitionFailure final {
    /// 失败发生的 Acquisition 状态机阶段。
    AcquisitionFailurePhase phase{AcquisitionFailurePhase::Admission};
    /// 调用者无需解析文本即可处理的稳定原因。
    AcquisitionFailureReason reason{AcquisitionFailureReason::BoardContractViolation};
    /// 对应 Prepare 身份；尚未分配时无效。
    board::PrepareCallId prepare_call{};
    /// Prepare 成功后得到的实际执行身份；此前失败时无效。
    board::PreparedExecutionId prepared{};
    /// 对应 Run 身份；Prepare 阶段失败时仅表示预留身份。
    board::BoardRunId run{};
    /// 与 run 配对的 Run generation。
    board::RunGeneration generation{};
    /// 同步 BoardError 有效时为 true；异步失败终态不伪造 BoardError。
    bool has_board_error{false};
    board::BoardErrc board_error{board::BoardErrc::ContractViolation};
};

}  // namespace vna::acquisition
