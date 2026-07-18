#include "vna/instrument/sweep_admission.hpp"

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

    auto runtime_reservation_result = runtime_.reserve_work(work_id);
    if (!runtime_reservation_result.has_value()) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::RuntimeAdmissionRejected});
    }
    auto runtime_reservation =
        std::move(runtime_reservation_result).take_value();

    auto terminal_reservation_result = store_.reserve_lifecycle_terminal();
    if (!terminal_reservation_result.has_value()) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::StoreAdmissionRejected});
    }
    auto terminal_reservation =
        std::move(terminal_reservation_result).take_value();

    auto accepted_commit = store_.commit_accepted(
        operation, std::move(terminal_reservation));
    if (std::holds_alternative<store::RejectedAcceptedCommit>(accepted_commit)) {
        return core::Result<AcceptedSweepOperation, SweepAdmissionError>::failure(
            SweepAdmissionError{SweepAdmissionErrc::StoreInitialCommitRejected});
    }

    mappings_[mapping_index] = Mapping{true, work_id, operation, &completion};
    auto dispatched = runtime_.dispatch(
        std::move(runtime_reservation),
        work,
        runtime::RuntimeCompletionRegistration{*this});
    if (!dispatched.has_value()) {
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

    auto mapping = mappings_[index];
    mappings_[index] = Mapping{};
    const auto terminal_state =
        terminal.kind == runtime::RuntimeTerminalKind::Completed
            ? store::OperationState::Completed
            : store::OperationState::Failed;
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
