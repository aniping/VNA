#include "runtime/function/instrument/sweep_admission.h"

#include <utility>
#include <variant>

namespace vna::instrument {

SweepAdmissionController::SweepAdmissionController(
    runtime::OperationRuntime& runtime,
    store::InstrumentStore& store) noexcept
    : runtime_(runtime),
      store_(store),
      completion_receiver_(runtime.register_completion_receiver()) {}

core::Result<AcceptedSweepOperation, SweepAdmissionError>
SweepAdmissionController::submit(
    store::OperationId operation,
    runtime::WorkId work_id,
    runtime::ExecutionLimits limits,
    runtime::RuntimeWork& work,
    SweepCompletionSink& completion) noexcept {
    if (find_mapping(work_id) != kMaximumMappings) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::DuplicateWorkId});
    }

    const auto mapping_index = find_free_mapping();
    if (mapping_index == kMaximumMappings) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::ControllerCapacityExhausted});
    }

    // 先预留执行槽，但此时工作尚未入队；后续任一步失败都会由 RAII 归还它。
    auto runtime_reservation_result = runtime_.reserve_work(
        work_id, limits, completion_receiver_);
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
        work);
    if (!dispatched.has_value()) {
        // Accepted 已经对外可见，不能再把 submit() 回滚成“从未接受”。
        // 运行时拒绝本控制器刚签发的有效凭证属于内部契约异常，因此立即落 Failed。
        mappings_[mapping_index] = Mapping{};
        (void)store_.commit_terminal(operation, store::OperationState::Failed);
    }

    return core::Result<AcceptedSweepOperation, SweepAdmissionError>::success(
        AcceptedSweepOperation{operation, work_id});
}

bool SweepAdmissionController::run_one() noexcept {
    return runtime_.run_one(completion_receiver_, *this);
}

void SweepAdmissionController::on_runtime_terminal(
    runtime::WorkId work,
    runtime::RuntimeTerminal terminal) noexcept {
    const auto index = find_mapping(work);
    if (index == kMaximumMappings) {
        return;
    }

    auto mapping = mappings_[index];
    const auto is_draining =
        terminal.kind == runtime::RuntimeTerminalKind::Draining;
    if (is_draining) {
        // 父 Operation 可以失败终结，但映射继续保留到唯一 Drain terminal。
        mappings_[index].drain = terminal.drain;
        // 外部 completion 在父 Operation 终结后即可释放；先清空指针，避免
        // 回调重入完成 Drain 并复用同一映射槽后，外层再改写新映射。
        mappings_[index].completion = nullptr;
    } else {
        // 真实工作终态先摘除映射，防止完成回调重入时重复提交。
        mappings_[index] = Mapping{};
    }
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

void SweepAdmissionController::on_runtime_drain_terminal(
    runtime::WorkId work,
    runtime::RuntimeDrainTerminal terminal) noexcept {
    const auto index = find_mapping(work);
    if (index == kMaximumMappings || mappings_[index].drain != terminal.drain) {
        return;
    }
    // 具名 Drain 已到资源终态；本里程碑只闭合内部所有权，不创建 Store Drain 事实。
    mappings_[index] = Mapping{};
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
