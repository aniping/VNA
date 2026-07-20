#pragma once

#include "runtime/core/base/result.h"
#include "runtime/core/base/strong_id.h"
#include "runtime/function/acquisition/acquisition_result.h"
#include "runtime/function/operation/operation_runtime.h"
#include "runtime/resource/store/completed_sweep_bundle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace vna::store {

/// Store Event Journal 内的单调事件身份；0 为无效值。
using OperationEventId = core::StrongId<struct OperationEventIdTag>;

/// 仪器状态存储操作的失败原因。
enum class StoreErrc {
    /// 固定生命周期槽位全部被占用。
    ResourceExhausted,
    /// 预留凭证无效、已消费或不属于当前 Store。
    InvalidReservation,
    /// 操作 ID/强关联 WorkId/plan digest 无效，或把 Accepted 当作终态提交。
    InvalidOperation,
    /// 相同 OperationId 已经可见。
    DuplicateOperation,
    /// 找不到要提交终态的操作。
    OperationNotFound,
    /// A candidate 无效，或其 WorkId/plan digest 与 Accepted Operation 不匹配。
    InvalidCandidate,
    /// A candidate 通过身份关联后，被有 schema 的领域校验规则拒绝。
    CandidateValidationRejected,
    /// A candidate 已在本地完成 staging，但正式 revision 切换前写入被拒绝。
    CandidateWriteRejected,
    /// 已安装的生命周期终态预留仍无法完成 state-only 提交，Store 不变量失效。
    IntegrityFault
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
    /// 与 Operation 关联的非执行性 Runtime correlation；查询者不能据此派发工作。
    runtime::WorkId work{};
    /// 初始 Accepted 时冻结的计划摘要。
    core::StrongDigest plan_digest{};
};

/// 某个 Operation 等待边界在终态时冻结的事实。
struct OperationFenceSnapshot final {
    OperationId operation{};
    OperationState state{OperationState::Accepted};
    /// 与 Operation terminal、status 和 Event 相同的 Store revision。
    std::uint64_t revision{0U};
};

/// 仪器共享状态寄存器中最近一次 Operation 终态事实。
struct InstrumentStatusSnapshot final {
    OperationId operation{};
    OperationState state{OperationState::Accepted};
    /// 0 表示尚无终态；否则与同批 Operation/fence/Event 相同。
    std::uint64_t revision{0U};
};

/// Event Journal 中一次不可变的 Operation 终态通知事实。
struct OperationEventSnapshot final {
    OperationEventId id{};
    OperationId operation{};
    OperationState state{OperationState::Failed};
    /// 与 Operation terminal、status 和 fence 相同的 Store revision。
    std::uint64_t revision{0U};
    /// failure 字段是否保存一项完整 A-only 采集失败。
    bool has_acquisition_failure{false};
    acquisition::AcquisitionFailure failure{};
    /// 本 Event 是否引用同 revision 发布的 CompletedSweepBundle。
    bool has_completed_sweep{false};
    /// has_completed_sweep 为 true 时的正式 A snapshot ID。
    acquisition::CompletedSweepId completed_sweep{};
};

/// 当前正式数据 Catalog 中各数据阶段的发布数量。
struct PublicationCountSnapshot final {
    std::size_t completed_sweeps{0U};
    std::size_t measurements{0U};
    std::size_t stages{0U};
    std::size_t analyses{0U};
};

class InstrumentStore;
#if defined(VNA_ENABLE_STORE_CONTRACT_TEST_HOOKS)
class InstrumentStoreContractTestAccess;
#endif

/// move-only 的生命周期终态容量预留。
///
/// 其目的是在对外提交 Accepted 之前，先保证该操作将来一定有容量原子提交
/// terminal Operation、status、fence 和一个必达 Event。
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

/// A-only 成功 publication 原子提交后的回执。
struct CompletedSweepCommitReceipt final {
    OperationId operation{};
    acquisition::CompletedSweepId completed_sweep{};
    /// 与 A、Operation、status、fence 和 Event 共同使用的新 revision。
    std::uint64_t revision{0U};
};

/// A-only 成功 publication 被拒绝时的回执。
struct RejectedCompletedSweepCommit final {
    StoreError error{};
    /// Store 未取得所有权的完整 candidate，调用者仍须重试或显式 abort。
    acquisition::CandidateCommitLease reclaimed;
};

/// A-only success bundle 的封闭提交结果。
using CompletedSweepCommitResult = std::variant<
    CompletedSweepCommitReceipt,
    RejectedCompletedSweepCommit>;

/// Store 资源占用和修订号的只读快照。
struct StoreSnapshot final {
    /// 已预留但尚未公开为 Accepted 的生命周期数。
    std::size_t reserved_lifecycles{0U};
    /// 当前可以按 OperationId 查询的操作数。
    std::size_t visible_operations{0U};
    /// 最近一次状态变更后的全局修订号。
    std::uint64_t revision{0U};
    /// 已原子写入终态的 Operation Event 数量。
    std::size_t events{0U};
};

/// 固定容量的仪器操作生命周期存储。
///
/// 当前保存 A-only Operation 生命周期与固定容量 CompletedSweepBundle，不保存
/// B/Stage/C。所有接口均同步且不动态扩容，便于映射到 RTOS 有界资源模型。
class InstrumentStore final {
public:
    /// 可见操作与预留生命周期的编译期槽位上限。
    static constexpr std::size_t kMaximumOperations = 16U;
    /// 当前每项可见 Operation 最多产生一个终态 Event，故上限与操作槽一致。
    static constexpr std::size_t kMaximumEvents = kMaximumOperations;

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

    /// 原子安装带 Runtime/plan correlation 的 Accepted 生命周期。
    /// @param operation 对外可见的非 0 OperationId。
    /// @param work 只作为事实保存的非 0 WorkId；Store 不执行该能力。
    /// @param plan_digest 与本次冻结请求绑定的非 0 摘要。
    /// @param reservation 当前 Store 为终态、status/fence/Event 签发的完整预留；
    ///        成功时所有权转移进可见 Operation，失败时原样返还。
    /// @return 成功时返回统一 revision；任一身份非法或重复时返回拒绝分支。
    AcceptedCommitResult commit_accepted(
        OperationId operation,
        runtime::WorkId work,
        core::StrongDigest plan_digest,
        LifecycleTerminalReservation&& reservation) noexcept;

    /// 为已接受操作写入最终状态。
    /// @param operation 已经可见的操作 ID。
    /// @param terminal_state 必须为 Completed 或 Failed。
    /// @return 首次提交时更新状态和修订号；重复提交返回 AlreadyTerminal；
    ///         操作不存在或状态参数非法时返回错误。
    core::Result<TerminalCommitReceipt, StoreError> commit_terminal(
        OperationId operation,
        OperationState terminal_state) noexcept;

    /// 原子写入 A-only 失败的全部权威事实。
    /// @param operation 已经 Accepted 且安装终态预留的 OperationId。
    /// @param failure L4 返回的类型化阶段、原因与 Board 身份；按值复制进 Event。
    /// @return 首次调用时让 Operation、status、fence 和失败 Event 使用同一新
    ///         revision；重复终态返回 AlreadyTerminal，不追加第二个 Event；
    ///         Operation 不存在时返回 OperationNotFound。若已安装预留仍无法写入，
    ///         返回 IntegrityFault 且不改变任何公开事实，调用者必须进入 fail-stop。
    core::Result<TerminalCommitReceipt, StoreError> commit_acquisition_failed(
        OperationId operation,
        acquisition::AcquisitionFailure failure) noexcept;

    /// 原子发布 A-only 成功的全部权威事实。
    /// @param operation 已经 Accepted 且安装终态预留的 OperationId。
    /// @param candidate worker 返回、仍拥有全部正式观测的 move-only 候选；成功
    ///        时被消费，拒绝时在结果中原样返还。
    /// @return 成功时 A、Completed Operation、status、fence 和 Event 共享同一
    ///         revision。身份/关联不匹配返回 InvalidCandidate；领域校验拒绝返回
    ///         CandidateValidationRejected；本地 staging 后、revision 切换前的
    ///         写入拒绝返回 CandidateWriteRejected；Operation 不存在返回
    ///         OperationNotFound。所有拒绝都不改变 revision 或任何正式事实，并在
    ///         RejectedCompletedSweepCommit 中原样返还完整 candidate 所有权。
    CompletedSweepCommitResult commit_completed_sweep(
        OperationId operation,
        acquisition::CandidateCommitLease&& candidate) noexcept;

    /// 按 ID 查询操作状态。
    /// @param operation 要查询的操作 ID。
    /// @return 找到时返回值拷贝；不存在时返回 std::nullopt。
    std::optional<OperationSnapshot> inspect_operation(
        OperationId operation) const noexcept;

    /// 按关联 Operation 查询已发布的不可变 A 层快照。
    /// @param operation 产生该快照的 OperationId。
    /// @return 发布完成时返回独立值副本；Accepted/Failed/不存在时为空。返回值
    ///         不包含 Store 内部裸 Buffer，生命周期由调用者副本自身决定。
    std::optional<CompletedSweepBundle> inspect_completed_sweep(
        OperationId operation) const noexcept;

    /// 查询某个 Operation 已提交的终态 fence。
    /// @param operation 要查询的 OperationId。
    /// @return 终态已提交时返回值副本；Accepted 或不存在时返回 std::nullopt。
    std::optional<OperationFenceSnapshot> inspect_fence(
        OperationId operation) const noexcept;

    /// @return 最近一次终态提交产生的共享状态副本；尚无终态时 revision 为 0。
    InstrumentStatusSnapshot inspect_status() const noexcept;

    /// @return Event Journal 中 revision 最新的终态 Event；尚无 Event 时为空。
    std::optional<OperationEventSnapshot> latest_event() const noexcept;

    /// @return A/B/Stage/C 正式发布数量的一致性副本。
    PublicationCountSnapshot inspect_publications() const noexcept;

    /// @return 当前容量使用和全局修订号快照。
    StoreSnapshot inspect() const noexcept;

private:
    friend class LifecycleTerminalReservation;
#if defined(VNA_ENABLE_STORE_CONTRACT_TEST_HOOKS)
    friend class InstrumentStoreContractTestAccess;
#endif

    enum class SlotState {
        Empty,
        Reserved,
        Visible
    };

    struct Slot final {
        SlotState slot_state{SlotState::Empty};
        std::uint64_t generation{0U};
        OperationSnapshot operation{};
        bool fence_visible{false};
        OperationFenceSnapshot fence{};
        bool event_visible{false};
        OperationEventSnapshot event{};
        std::optional<CompletedSweepBundle> completed_sweep{};
    };

    void release_reservation(
        std::size_t slot,
        std::uint64_t generation) noexcept;
    AcceptedCommitResult commit_accepted_impl(
        OperationId operation,
        runtime::WorkId work,
        core::StrongDigest plan_digest,
        bool require_correlation,
        LifecycleTerminalReservation&& reservation) noexcept;
    core::Result<TerminalCommitReceipt, StoreError> commit_terminal_impl(
        OperationId operation,
        OperationState terminal_state,
        const acquisition::AcquisitionFailure* failure) noexcept;

    std::array<Slot, kMaximumOperations> slots_{};
    std::size_t capacity_{0U};
    std::uint64_t next_generation_{1U};
    std::uint64_t revision_{0U};
    std::size_t events_{0U};
    std::uint64_t next_event_id_{1U};
    InstrumentStatusSnapshot status_{};
    PublicationCountSnapshot publications_{};
#if defined(VNA_ENABLE_STORE_CONTRACT_TEST_HOOKS)
    /// 只在合同测试组合存在；由 friend 设置并由下一次合法成功提交消费一次。
    std::optional<StoreErrc> next_completed_sweep_commit_fault_{};
    /// 只在合同测试组合存在；模拟已安装终态预留无法完成 state-only 失败提交。
    bool fail_next_acquisition_failure_commit_{false};
    /// 只在合同测试组合存在；模拟错误 OperationId/零 revision 的伪成功回执。
    bool return_malformed_acquisition_failure_receipt_{false};
#endif
};

}  // namespace vna::store
