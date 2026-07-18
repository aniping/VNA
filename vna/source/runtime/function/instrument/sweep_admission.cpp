#include "runtime/function/instrument/sweep_admission.h"

#include <utility>
#include <variant>

namespace vna::instrument {

SweepAdmissionController::SweepAdmissionController(
    runtime::OperationRuntime& runtime,
    store::InstrumentStore& store) noexcept
    : runtime_(runtime), store_(store) {}

core::Result<AcceptedSweepOperation, SweepAdmissionError>
SweepAdmissionController::submit(
    store::OperationId operation,
    runtime::WorkId work_id,
    runtime::RuntimeWork& work,
    SweepCompletionSink& completion) noexcept {
    const auto mapping_index = find_free_mapping();
    if (mapping_index == kMaximumMappings) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::ControllerCapacityExhausted});
    }

    // 先预留执行槽，但此时工作尚未入队；后续任一步失败都会由 RAII 归还它。
    auto runtime_reservation_result = runtime_.reserve_work(work_id);
    if (!runtime_reservation_result.has_value()) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::RuntimeAdmissionRejected});
    }
    auto runtime_reservation =
        std::move(runtime_reservation_result).take_value();

    // 在公开 Accepted 之前预留终态容量，保证已接受操作最终一定能落盘。
    auto terminal_reservation_result = store_.reserve_lifecycle_terminal();
    if (!terminal_reservation_result.has_value()) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::StoreAdmissionRejected});
    }
    auto terminal_reservation =
        std::move(terminal_reservation_result).take_value();

    // 这是可见性边界：提交失败时工作仍未派发，外部也看不到半成品操作。
    auto accepted_commit = store_.commit_accepted(
        operation, std::move(terminal_reservation));
    if (std::holds_alternative<store::RejectedAcceptedCommit>(accepted_commit)) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::StoreInitialCommitRejected});
    }

    // 在派发前建立反向映射，因为运行时终态只携带 WorkId，而 Store 使用 OperationId。
    mappings_[mapping_index] = Mapping{true, work_id, operation, &completion};
    auto dispatched = runtime_.dispatch(
        std::move(runtime_reservation),
        work,
        runtime::RuntimeCompletionRegistration{*this});
    if (!dispatched.has_value()) {
        // Accepted 已经对外可见，不能再把 submit() 回滚成“从未接受”。
        // 运行时拒绝本控制器刚签发的有效凭证属于内部契约异常，因此立即落 Failed。
        mappings_[mapping_index] = Mapping{};
        (void)store_.commit_terminal(operation, store::OperationState::Failed);
    }

    return core::Result<AcceptedSweepOperation, SweepAdmissionError>::success(
        AcceptedSweepOperation{operation, work_id});
}

void SweepAdmissionController::on_runtime_terminal(
    runtime::WorkId work,
    runtime::RuntimeTerminal terminal) noexcept {
    const auto index = find_mapping(work);
    if (index == kMaximumMappings) {
        return;
    }

    // 先摘除映射，防止完成回调重入时对同一 WorkId 重复提交终态。
    auto mapping = mappings_[index];
    mappings_[index] = Mapping{};
    const auto terminal_state =
        terminal.kind == runtime::RuntimeTerminalKind::Completed
            ? store::OperationState::Completed
            : store::OperationState::Failed;
    // 只有终态已经可靠写入 Store 后，才向上层通知可查询的最终快照。
    const auto committed = store_.commit_terminal(mapping.operation, terminal_state);
    if (committed.has_value()) {
        const auto snapshot = store_.inspect_operation(mapping.operation);
        if (snapshot.has_value()) {
            mapping.completion->on_sweep_terminal(*snapshot);
        }
    }
}

std::size_t SweepAdmissionController::find_free_mapping() const noexcept {
    for (std::size_t index = 0U; index < mappings_.size(); ++index) {
        if (!mappings_[index].active) {
            return index;
        }
    }
    return kMaximumMappings;
}

std::size_t SweepAdmissionController::find_mapping(
    runtime::WorkId work) const noexcept {
    for (std::size_t index = 0U; index < mappings_.size(); ++index) {
        if (mappings_[index].active && mappings_[index].work == work) {
            return index;
        }
    }
    return kMaximumMappings;
}

}  // namespace vna::instrument
