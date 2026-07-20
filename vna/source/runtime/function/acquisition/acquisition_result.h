#pragma once

#include "runtime/function/acquisition/a_only_completion_owners.h"
#include "runtime/function/acquisition/candidate_commit_lease.h"
#include "runtime/function/acquisition/network_observation_error.h"
#include "runtime/platform/board/board_port.h"

#include <cstdint>

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
    /// 必需观测、实际轴与成功 terminal 闭合并密封 candidate 的阶段。
    CandidateSealing,
    /// L2 把 worker candidate 原子提交到 Store 的阶段。
    PublicationCommit,
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
    /// Board 接受 Prepare 后通过携带 cleanup evidence 的唯一终态报告失败。
    BoardPrepareFailed,
    /// Board 通过唯一异步终态报告 Run 失败。
    BoardTerminalFailed,
    /// Board Prepare 已移交仍在排空的资源 owner，执行容量必须隔离保留。
    BoardPrepareDraining,
    /// Prepared Manifest 超出准入时冻结的保守资源范围。
    ManifestOutsideAdmission,
    /// Board 回调的身份、顺序或数量违反公开契约。
    BoardContractViolation,
    /// 成功 terminal 到达时必需观测或点覆盖仍不完整。
    IncompleteObservationSet,
    /// Store 拒绝了已经密封的 A publication candidate。
    StoreCommitRejected,
    /// Accepted 后 Runtime 拒绝了此前签发的有效派发能力。
    RuntimeDispatchContractViolation
};

/// 调用者处理 A-only 采集失败时采用的稳定重试分类。
enum class AcquisitionRetryClass {
    /// 原请求或能力不受支持；修改请求、配置或实现前不得原样重试。
    DoNotRetryWithoutChange,
    /// 版本或实际执行 cut 已变化；重新读取能力并重新规划后才可重试。
    AfterStateRefresh,
    /// 容量或异步 owner 尚未释放；观察资源终态后才可重试。
    AfterResourceRelease,
    /// 停止或取消由调用者发起；只有新的显式请求才能重新开始。
    ExplicitResubmission,
    /// Board/Runtime 契约或执行失败；完成诊断、恢复或隔离处置后才可重试。
    AfterRecovery
};

/// A-only 失败时已经取得的执行生命周期证据。
///
/// 该分类只描述 Mock-only 上层流程观察到的“Run 是否接受/终态是否到达”，
/// 不是物理 RF-off、互锁或真实单板安全证明。Real Board 仍必须通过独立
/// SafetyLane、abort/readback 和 HIL 验收。
enum class AcquisitionSafetyImpact {
    /// Board Run 从未被接受；不据此推断真实硬件没有其他 RF 活动。
    NoRunAccepted,
    /// 匹配 Run terminal 已到达；terminal 本身不等价于物理 RF-off readback。
    RunTerminalObserved,
    /// 存在尚未闭合或违反契约的异步义务，相关资源必须继续隔离。
    ResourceIsolationRequired
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
    /// 同步拒绝或 PrepareFailed terminal 携带 BoardError 时为 true；
    /// 只有 RunTerminalKind 的失败不伪造 BoardError。
    bool has_board_error{false};
    /// has_board_error 为 true 时保存稳定 Board 错误码；否则不得据此分支。
    board::BoardErrc board_error{board::BoardErrc::ContractViolation};
    /// 无需解析诊断文本即可执行的重试前置条件。
    AcquisitionRetryClass retry{AcquisitionRetryClass::AfterRecovery};
    /// 当前失败已经取得的执行生命周期证据；不宣称物理 RF 安全。
    AcquisitionSafetyImpact safety{
        AcquisitionSafetyImpact::ResourceIsolationRequired};
    /// PrepareSucceeded 已返回 Manifest 时保存其身份；此前失败时无效。
    board::ManifestId manifest{};
    /// Manifest 声明的 BoardSessionId；此前失败时无效。
    board::BoardSessionId board_session{};
    /// Manifest 声明的 capability revision；此前失败时为 0。
    std::uint64_t capability_revision{0U};
    /// observation_error 是否保存观测账本或 Ingress 失败证据。
    bool has_observation_error{false};
    /// has_observation_error 为 true 时保存有界身份、覆盖和 terminal 摘要。
    NetworkObservationError observation_error{};
    /// contract_violation 是否保存 Board 回调身份或唯一终态违约证据。
    bool has_contract_violation{false};
    /// has_contract_violation 为 true 时保存首个稳定、固定大小的违约事实。
    board::BoardContractViolation contract_violation{};
};

/// L4 成功终态移交给 L2 的完整 publication 与 purpose-specific owner 集合。
///
/// candidate 只能进入 InstrumentStore commit/abort；completion_owners 必须留在
/// L2，直到 Store receipt 或失败事实可见后才终结，两者不能被合并进 Store。
struct AcquisitionSucceeded final {
    /// worker 密封且提交前不可查询的正式 A 候选 owner。
    CandidateCommitLease candidate;
    /// 匹配 A-only completion 与 disabled Preview 的 L2 owner 聚合。
    AOnlyCompletionOwners completion_owners;
};

}  // namespace vna::acquisition
