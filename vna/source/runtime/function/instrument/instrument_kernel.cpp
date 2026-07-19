#include "runtime/function/instrument/instrument_kernel.h"

#include <cstring>
#include <limits>
#include <utility>
#include <variant>

namespace vna::instrument {
namespace {

core::StrongDigest digest_request(const AOnlySweepRequest& request) noexcept {
    std::uint64_t start_bits{0U};
    std::uint64_t stop_bits{0U};
    static_assert(sizeof(start_bits) == sizeof(request.start_hz));
    std::memcpy(&start_bits, &request.start_hz, sizeof(start_bits));
    std::memcpy(&stop_bits, &request.stop_hz, sizeof(stop_bits));
    auto value = 0xA011DA7A00000000ULL ^
        static_cast<std::uint64_t>(request.point_count);
    value ^= start_bits + 0x9E3779B97F4A7C15ULL + (value << 6U) + (value >> 2U);
    value ^= stop_bits + 0x9E3779B97F4A7C15ULL + (value << 6U) + (value >> 2U);
    return core::StrongDigest{value == 0U ? 1U : value};
}

}  // namespace

InstrumentKernel::InstrumentKernel(
    runtime::OperationRuntime& runtime,
    store::InstrumentStore& store,
    board::BoardExecutionPort& board_execution,
    acquisition::AcquisitionAdmissionPool& acquisition_resources,
    runtime::RuntimeMonotonicClock& clock,
    AOnlyKernelProfile profile) noexcept
    : runtime_(runtime),
      store_(store),
      board_execution_(board_execution),
      acquisition_resources_(acquisition_resources),
      clock_(clock),
      profile_(profile),
      completion_receiver_(runtime.register_completion_receiver()) {}

AOnlySubmitResult InstrumentKernel::submit_a_only(
    const AOnlySweepRequest& request) noexcept {
    const auto reject = [](AOnlySubmitErrc code) noexcept {
        return AOnlySubmitResult::failure(AOnlySubmitError{code});
    };
    if (!request.authorization.valid()) {
        return reject(AOnlySubmitErrc::DiagnosticAuthorizationRequired);
    }

    const auto capabilities = board_execution_.capabilities();
    const bool capability_cut_valid = capabilities.session_id.valid() &&
        capabilities.capability_revision != 0U &&
        capabilities.topology_epoch != 0U &&
        capabilities.operational_epoch != 0U;
    if (!capability_cut_valid ||
        (request.expected_capability_revision != 0U &&
         request.expected_capability_revision !=
             capabilities.capability_revision)) {
        return reject(AOnlySubmitErrc::RevisionConflict);
    }
    const auto board_now = board_execution_.monotonic_tick();
    const bool request_is_valid = request.point_count > 0U &&
        request.point_count <= capabilities.maximum_points &&
        request.start_hz > 0.0 &&
        (request.point_count == 1U
             ? request.stop_hz == request.start_hz
             : request.stop_hz > request.start_hz);
    const auto now = clock_.now_tick();
    const auto maximum_tick = std::numeric_limits<std::uint64_t>::max();
    const bool profile_is_valid = profile_.deadline_span_ticks > 0U &&
        profile_.budget_units > 0U && profile_.budget_units != maximum_tick &&
        profile_.deadline_span_ticks < maximum_tick - now &&
        profile_.board_continuation_span_ticks > 0U &&
        profile_.board_continuation_span_ticks < maximum_tick - board_now;
    if (!request_is_valid || !profile_is_valid) {
        return reject(AOnlySubmitErrc::InvalidRequest);
    }

    const auto slot_index = find_free_slot();
    if (slot_index == kMaximumAOnlyOperations) {
        return reject(AOnlySubmitErrc::ControllerCapacityExhausted);
    }

    const auto plan_digest = digest_request(request);
    const board::SweepIntent intent{
        request.point_count, request.start_hz, request.stop_hz, plan_digest};
    auto board_reservation_result = board_execution_.reserve_execution();
    if (!board_reservation_result.has_value()) {
        return reject(AOnlySubmitErrc::AcquisitionResourcesUnavailable);
    }
    auto board_reservation =
        std::move(board_reservation_result).take_value();
    auto acquisition_result = acquisition_resources_.reserve(
        acquisition::AcquisitionAdmissionPool::Claim{
            plan_digest,
            capabilities,
            request.point_count,
            static_cast<std::uint32_t>(kAOnlyChunkCapacity),
            request.start_hz,
            request.stop_hz});
    if (!acquisition_result.has_value()) {
        return reject(AOnlySubmitErrc::AcquisitionResourcesUnavailable);
    }
    auto acquisition_lease = std::move(acquisition_result).take_value();

    const auto operation = store::OperationId{next_operation_id_++};
    const auto work = runtime::WorkId{next_work_id_++};
    const auto snapshot_id = acquisition::CompletedSweepId{
        next_completed_sweep_id_++};
    const auto logical_sweep_id = acquisition::LogicalSweepId{
        next_logical_sweep_id_++};
    auto delivery_result = acquisition_buffers_.reserve_delivery(
        work.value(), kAOnlyChunkCapacity);
    if (!delivery_result.has_value()) {
        return reject(AOnlySubmitErrc::AcquisitionResourcesUnavailable);
    }
    auto delivery = std::move(delivery_result).take_value();
    const runtime::ExecutionLimits limits{
        now + profile_.deadline_span_ticks, profile_.budget_units};
    auto runtime_result = runtime_.reserve_work(
        work, limits, completion_receiver_);
    if (!runtime_result.has_value()) {
        return reject(AOnlySubmitErrc::RuntimeAdmissionRejected);
    }
    auto runtime_reservation = std::move(runtime_result).take_value();

    auto store_result = store_.reserve_lifecycle_terminal();
    if (!store_result.has_value()) {
        return reject(AOnlySubmitErrc::StoreAdmissionRejected);
    }
    auto terminal_reservation = std::move(store_result).take_value();

    auto& slot = slots_[slot_index];
    auto prepare_authorization = board::PrepareAuthorization::issue(
        capabilities.session_id,
        capabilities.session_epoch,
        capabilities.capability_revision,
        capabilities.topology_epoch,
        capabilities.operational_epoch,
        plan_digest);
    const auto continuation_digest = core::StrongDigest{
        plan_digest.value ^ 0xC0171A7100000001ULL};
    slot.engine.emplace(
        board_execution_,
        intent,
        std::move(prepare_authorization),
        board::PrepareCallId{work.value()},
        board::BoardRunId{work.value()},
        board::RunGeneration{work.value()},
        snapshot_id,
        logical_sweep_id,
        work,
        kAOnlyChunkCapacity,
        board::AcquisitionContinuationAttestation{
            continuation_digest,
            board_now + profile_.board_continuation_span_ticks},
        std::move(delivery),
        runtime::DrainId{work.value()},
        std::move(acquisition_lease),
        std::move(board_reservation));

    auto accepted_commit = store_.commit_accepted(
        operation, work, plan_digest, std::move(terminal_reservation));
    if (std::holds_alternative<store::RejectedAcceptedCommit>(accepted_commit)) {
        slot.engine.reset();
        return reject(AOnlySubmitErrc::StoreInitialCommitRejected);
    }

    slot.active = true;
    slot.work = work;
    slot.operation = operation;
    auto dispatched = runtime_.dispatch(
        std::move(runtime_reservation), *slot.engine);
    if (!dispatched.has_value()) {
        (void)store_.commit_acquisition_failed(
            operation,
            acquisition::AcquisitionFailure{
                acquisition::AcquisitionFailurePhase::RuntimeDispatch,
                acquisition::AcquisitionFailureReason::
                    RuntimeDispatchContractViolation,
                board::PrepareCallId{},
                board::PreparedExecutionId{},
                board::BoardRunId{},
                board::RunGeneration{},
                false,
                board::BoardErrc::ContractViolation,
                acquisition::AcquisitionRetryClass::AfterRecovery,
                acquisition::AcquisitionSafetyImpact::NoRunAccepted});
        (void)slot.engine->finalize_failure_owners();
        slot.release_pending = true;
        release_completed_slots();
    }

    return AOnlySubmitResult::success(AcceptedAOnlyOperation{operation});
}

bool InstrumentKernel::run_one() noexcept {
    const auto progressed = runtime_.run_one(completion_receiver_, *this);
    release_completed_slots();
    return progressed;
}

void InstrumentKernel::on_runtime_terminal(
    runtime::WorkId work,
    runtime::RuntimeTerminal terminal) noexcept {
    const auto index = find_slot(work);
    if (index == kMaximumAOnlyOperations) {
        return;
    }
    auto& slot = slots_[index];
    if (terminal.kind == runtime::RuntimeTerminalKind::Completed) {
        auto success = slot.engine->take_success();
        if (!success.has_value()) {
            const auto committed = store_.commit_acquisition_failed(
                slot.operation,
                acquisition::AcquisitionFailure{
                    acquisition::AcquisitionFailurePhase::CandidateSealing,
                    acquisition::AcquisitionFailureReason::
                        IncompleteObservationSet,
                    board::PrepareCallId{},
                    board::PreparedExecutionId{},
                    board::BoardRunId{},
                    board::RunGeneration{},
                    false,
                    board::BoardErrc::ContractViolation,
                    acquisition::AcquisitionRetryClass::AfterRecovery,
                    acquisition::AcquisitionSafetyImpact::RunTerminalObserved});
            if (committed.has_value()) {
                (void)slot.engine->finalize_failure_owners();
                slot.release_pending = true;
            }
            return;
        }

        auto commit = store_.commit_completed_sweep(
            slot.operation, std::move(success->candidate));
        if (auto* receipt =
                std::get_if<store::CompletedSweepCommitReceipt>(&commit)) {
            if (success->completion_owners.finalize_published(
                    receipt->completed_sweep)) {
                slot.release_pending = true;
            } else {
                // 已发布 A 不能回滚；无法终结的 purpose-specific owner 保持隔离。
                slot.pending_success.emplace(std::move(*success));
            }
            return;
        }

        auto rejected = std::get<store::RejectedCompletedSweepCommit>(
            std::move(commit));
        success->candidate = std::move(rejected.reclaimed);
        const auto failed = store_.commit_acquisition_failed(
            slot.operation,
            acquisition::AcquisitionFailure{
                acquisition::AcquisitionFailurePhase::PublicationCommit,
                acquisition::AcquisitionFailureReason::StoreCommitRejected,
                board::PrepareCallId{},
                board::PreparedExecutionId{},
                board::BoardRunId{},
                board::RunGeneration{},
                false,
                board::BoardErrc::ContractViolation,
                acquisition::AcquisitionRetryClass::AfterRecovery,
                acquisition::AcquisitionSafetyImpact::RunTerminalObserved});
        if (!failed.has_value()) {
            slot.pending_success.emplace(std::move(*success));
            return;
        }
        (void)success->candidate.abort();
        (void)success->completion_owners.finalize_failed();
        slot.release_pending = true;
        return;
    }

    const auto committed = store_.commit_acquisition_failed(
        slot.operation, slot.engine->failure());
    if (!committed.has_value()) {
        return;
    }

    if (terminal.kind != runtime::RuntimeTerminalKind::Draining) {
        (void)slot.engine->finalize_failure_owners();
        slot.release_pending = true;
    }
}

void InstrumentKernel::on_runtime_drain_terminal(
    runtime::WorkId work,
    runtime::RuntimeDrainTerminal terminal) noexcept {
    const auto index = find_slot(work);
    if (index == kMaximumAOnlyOperations) {
        return;
    }
    auto& slot = slots_[index];
    if (terminal.kind != runtime::RuntimeDrainTerminalKind::Drained) {
        // Quarantined/CleanupFailed 表示 owner 仍未安全释放；保持 Engine、上层
        // 资源槽和 Board execution reservation 隔离，不能回收到新提交。
        return;
    }
    (void)slot.engine->finalize_failure_owners();
    slot.release_pending = true;
}

std::size_t InstrumentKernel::find_free_slot() const noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        if (!slots_[index].active) {
            return index;
        }
    }
    return kMaximumAOnlyOperations;
}

std::size_t InstrumentKernel::find_slot(runtime::WorkId work) const noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        if (slots_[index].active && slots_[index].work == work) {
            return index;
        }
    }
    return kMaximumAOnlyOperations;
}

void InstrumentKernel::release_completed_slots() noexcept {
    for (auto& slot : slots_) {
        if (slot.release_pending) {
            slot.engine.reset();
            slot.active = false;
            slot.release_pending = false;
            slot.work = runtime::WorkId{};
            slot.operation = store::OperationId{};
            slot.pending_success.reset();
        }
    }
}

}  // namespace vna::instrument
