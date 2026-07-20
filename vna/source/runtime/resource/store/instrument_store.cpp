#include "runtime/resource/store/instrument_store.h"

#include <algorithm>
#include <utility>

namespace vna::store {

LifecycleTerminalReservation::LifecycleTerminalReservation(
    InstrumentStore& owner,
    std::size_t slot,
    std::uint64_t generation) noexcept
    : owner_(&owner), slot_(slot), generation_(generation) {}

LifecycleTerminalReservation::LifecycleTerminalReservation(
    LifecycleTerminalReservation&& other) noexcept
    : owner_(other.owner_), slot_(other.slot_), generation_(other.generation_) {
    other.invalidate();
}

LifecycleTerminalReservation& LifecycleTerminalReservation::operator=(
    LifecycleTerminalReservation&& other) noexcept {
    if (this != &other) {
        release();
        owner_ = other.owner_;
        slot_ = other.slot_;
        generation_ = other.generation_;
        other.invalidate();
    }
    return *this;
}

LifecycleTerminalReservation::~LifecycleTerminalReservation() {
    // 未安装为可见生命周期的预留自动回滚，覆盖所有提前返回路径。
    release();
}

void LifecycleTerminalReservation::release() noexcept {
    if (owner_ != nullptr) {
        owner_->release_reservation(slot_, generation_);
        invalidate();
    }
}

void LifecycleTerminalReservation::invalidate() noexcept {
    owner_ = nullptr;
    slot_ = 0U;
    generation_ = 0U;
}

InstrumentStore::InstrumentStore(std::size_t capacity) noexcept
    : capacity_(std::min(capacity, kMaximumOperations)) {}

core::Result<LifecycleTerminalReservation, StoreError>
InstrumentStore::reserve_lifecycle_terminal() noexcept {
    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.slot_state != SlotState::Empty) {
            continue;
        }

        // generation 把凭证绑定到本次占用，避免旧凭证释放复用后的同一槽位。
        slot.slot_state = SlotState::Reserved;
        slot.generation = next_generation_++;
        return core::Result<LifecycleTerminalReservation, StoreError>::success(
            LifecycleTerminalReservation{*this, index, slot.generation});
    }

    return core::Result<LifecycleTerminalReservation, StoreError>::failure(
        StoreError{StoreErrc::ResourceExhausted});
}

AcceptedCommitResult InstrumentStore::commit_accepted(
    OperationId operation,
    LifecycleTerminalReservation&& reservation) noexcept {
    return commit_accepted_impl(
        operation,
        runtime::WorkId{},
        core::StrongDigest{},
        false,
        std::move(reservation));
}

AcceptedCommitResult InstrumentStore::commit_accepted(
    OperationId operation,
    runtime::WorkId work,
    core::StrongDigest plan_digest,
    LifecycleTerminalReservation&& reservation) noexcept {
    return commit_accepted_impl(
        operation, work, plan_digest, true, std::move(reservation));
}

AcceptedCommitResult InstrumentStore::commit_accepted_impl(
    OperationId operation,
    runtime::WorkId work,
    core::StrongDigest plan_digest,
    bool require_correlation,
    LifecycleTerminalReservation&& reservation) noexcept {
    const auto reject = [&](StoreErrc code) -> AcceptedCommitResult {
        // 初始提交失败不能吞掉调用者预留的终态容量，必须原样返还凭证。
        return RejectedAcceptedCommit{
            StoreError{code}, std::move(reservation)};
    };

    if (!operation.valid() ||
        (require_correlation && (!work.valid() || !plan_digest.valid()))) {
        return reject(StoreErrc::InvalidOperation);
    }
    if (!reservation.valid() || reservation.owner_ != this ||
        reservation.slot_ >= capacity_) {
        return reject(StoreErrc::InvalidReservation);
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        if (slots_[index].slot_state == SlotState::Visible &&
            slots_[index].operation.id == operation) {
            return reject(StoreErrc::DuplicateOperation);
        }
    }

    auto& slot = slots_[reservation.slot_];
    if (slot.slot_state != SlotState::Reserved ||
        slot.generation != reservation.generation_) {
        return reject(StoreErrc::InvalidReservation);
    }

    // 从 Reserved 直接转换为 Visible，使 Accepted 状态与终态容量成为同一槽位，
    // 不存在“外部看见 Accepted，但内部没有空间记录最终结果”的窗口。
    slot.slot_state = SlotState::Visible;
    slot.operation = OperationSnapshot{
        operation, OperationState::Accepted, ++revision_, work, plan_digest};
    reservation.invalidate();
    return AcceptedCommitReceipt{operation, revision_};
}

core::Result<TerminalCommitReceipt, StoreError> InstrumentStore::commit_terminal(
    OperationId operation,
    OperationState terminal_state) noexcept {
    return commit_terminal_impl(operation, terminal_state, nullptr);
}

core::Result<TerminalCommitReceipt, StoreError>
InstrumentStore::commit_acquisition_failed(
    OperationId operation,
    acquisition::AcquisitionFailure failure) noexcept {
#if defined(VNA_ENABLE_STORE_CONTRACT_TEST_HOOKS)
    if (return_malformed_acquisition_failure_receipt_) {
        return_malformed_acquisition_failure_receipt_ = false;
        return core::Result<TerminalCommitReceipt, StoreError>::success(
            TerminalCommitReceipt{
                OperationId{},
                OperationState::Failed,
                0U,
                TerminalCommitDisposition::Committed});
    }
    if (fail_next_acquisition_failure_commit_) {
        fail_next_acquisition_failure_commit_ = false;
        return core::Result<TerminalCommitReceipt, StoreError>::failure(
            StoreError{StoreErrc::IntegrityFault});
    }
#endif
    return commit_terminal_impl(
        operation, OperationState::Failed, &failure);
}

CompletedSweepCommitResult InstrumentStore::commit_completed_sweep(
    OperationId operation,
    acquisition::CandidateCommitLease&& candidate) noexcept {
    const auto reject = [&](StoreErrc code) -> CompletedSweepCommitResult {
        return RejectedCompletedSweepCommit{
            StoreError{code}, std::move(candidate)};
    };
    if (!operation.valid() || !candidate.valid()) {
        return reject(StoreErrc::InvalidCandidate);
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.slot_state != SlotState::Visible ||
            slot.operation.id != operation) {
            continue;
        }
        if (slot.operation.state != OperationState::Accepted ||
            slot.operation.work != candidate.work() ||
            slot.operation.plan_digest != candidate.plan_digest()) {
            return reject(StoreErrc::InvalidCandidate);
        }

#if defined(VNA_ENABLE_STORE_CONTRACT_TEST_HOOKS)
        if (next_completed_sweep_commit_fault_ ==
            StoreErrc::CandidateValidationRejected) {
            next_completed_sweep_commit_fault_.reset();
            return reject(StoreErrc::CandidateValidationRejected);
        }
#endif

        const auto terminal_revision = revision_ + 1U;
        CompletedSweepBundle completed{
            operation, terminal_revision, candidate};

#if defined(VNA_ENABLE_STORE_CONTRACT_TEST_HOOKS)
        if (next_completed_sweep_commit_fault_ ==
            StoreErrc::CandidateWriteRejected) {
            // completed 只在栈上完成 staging；revision 与所有公开 Slot 字段尚未
            // 切换，因此拒绝后 candidate 仍由返回分支唯一持有且正式事实为零。
            next_completed_sweep_commit_fault_.reset();
            return reject(StoreErrc::CandidateWriteRejected);
        }
#endif

        // 所有有界字段准备完成后才切换 revision；本同步函数不会在中途暴露 Slot。
        revision_ = terminal_revision;
        slot.operation.state = OperationState::Completed;
        slot.operation.revision = terminal_revision;
        slot.completed_sweep.emplace(std::move(completed));
        slot.fence_visible = true;
        slot.fence = OperationFenceSnapshot{
            operation, OperationState::Completed, terminal_revision};
        status_ = InstrumentStatusSnapshot{
            operation, OperationState::Completed, terminal_revision};
        slot.event_visible = true;
        slot.event = OperationEventSnapshot{
            OperationEventId{next_event_id_++},
            operation,
            OperationState::Completed,
            terminal_revision,
            false,
            acquisition::AcquisitionFailure{},
            true,
            candidate.snapshot_id()};
        ++events_;
        ++publications_.completed_sweeps;
        const auto published_id = candidate.snapshot_id();
        (void)candidate.abort();
        return CompletedSweepCommitReceipt{
            operation, published_id, terminal_revision};
    }

    return reject(StoreErrc::OperationNotFound);
}

core::Result<TerminalCommitReceipt, StoreError>
InstrumentStore::commit_terminal_impl(
    OperationId operation,
    OperationState terminal_state,
    const acquisition::AcquisitionFailure* failure) noexcept {
    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.slot_state != SlotState::Visible ||
            slot.operation.id != operation) {
            continue;
        }

        if (slot.operation.state != OperationState::Accepted) {
            // 终态提交具备幂等读取语义：不覆盖首个终态，也不递增修订号。
            return core::Result<TerminalCommitReceipt, StoreError>::success(
                TerminalCommitReceipt{
                    operation,
                    slot.operation.state,
                    slot.operation.revision,
                    TerminalCommitDisposition::AlreadyTerminal});
        }
        if (terminal_state == OperationState::Accepted) {
            return core::Result<TerminalCommitReceipt, StoreError>::failure(
                StoreError{StoreErrc::InvalidOperation});
        }

        // 同一个生命周期槽在 Accepted 前已经为终态、fence、status 和 Event
        // 保留容量；以下字段用一次 revision 切换，不存在半可见窗口。
        const auto terminal_revision = ++revision_;
        slot.operation.state = terminal_state;
        slot.operation.revision = terminal_revision;
        slot.fence_visible = true;
        slot.fence = OperationFenceSnapshot{
            operation, terminal_state, terminal_revision};
        status_ = InstrumentStatusSnapshot{
            operation, terminal_state, terminal_revision};
        slot.event_visible = true;
        slot.event = OperationEventSnapshot{
            OperationEventId{next_event_id_++},
            operation,
            terminal_state,
            terminal_revision,
            failure != nullptr,
            failure == nullptr ? acquisition::AcquisitionFailure{} : *failure};
        ++events_;
        return core::Result<TerminalCommitReceipt, StoreError>::success(
            TerminalCommitReceipt{
                operation,
                terminal_state,
                terminal_revision,
                TerminalCommitDisposition::Committed});
    }

    return core::Result<TerminalCommitReceipt, StoreError>::failure(
        StoreError{StoreErrc::OperationNotFound});
}

std::optional<OperationSnapshot> InstrumentStore::inspect_operation(
    OperationId operation) const noexcept {
    for (std::size_t index = 0U; index < capacity_; ++index) {
        if (slots_[index].slot_state == SlotState::Visible &&
            slots_[index].operation.id == operation) {
            return slots_[index].operation;
        }
    }
    return std::nullopt;
}

std::optional<CompletedSweepBundle> InstrumentStore::inspect_completed_sweep(
    OperationId operation) const noexcept {
    for (std::size_t index = 0U; index < capacity_; ++index) {
        const auto& slot = slots_[index];
        if (slot.slot_state == SlotState::Visible &&
            slot.operation.id == operation && slot.completed_sweep.has_value()) {
            return *slot.completed_sweep;
        }
    }
    return std::nullopt;
}

std::optional<OperationFenceSnapshot> InstrumentStore::inspect_fence(
    OperationId operation) const noexcept {
    for (std::size_t index = 0U; index < capacity_; ++index) {
        const auto& slot = slots_[index];
        if (slot.slot_state == SlotState::Visible &&
            slot.operation.id == operation && slot.fence_visible) {
            return slot.fence;
        }
    }
    return std::nullopt;
}

InstrumentStatusSnapshot InstrumentStore::inspect_status() const noexcept {
    return status_;
}

std::optional<OperationEventSnapshot> InstrumentStore::latest_event() const noexcept {
    const OperationEventSnapshot* latest{nullptr};
    for (std::size_t index = 0U; index < capacity_; ++index) {
        const auto& slot = slots_[index];
        if (slot.event_visible &&
            (latest == nullptr || slot.event.revision > latest->revision)) {
            latest = &slot.event;
        }
    }
    return latest == nullptr
        ? std::optional<OperationEventSnapshot>{}
        : std::optional<OperationEventSnapshot>{*latest};
}

PublicationCountSnapshot InstrumentStore::inspect_publications() const noexcept {
    return publications_;
}

StoreSnapshot InstrumentStore::inspect() const noexcept {
    StoreSnapshot snapshot{};
    snapshot.revision = revision_;
    snapshot.events = events_;
    for (std::size_t index = 0U; index < capacity_; ++index) {
        if (slots_[index].slot_state == SlotState::Reserved) {
            ++snapshot.reserved_lifecycles;
        } else if (slots_[index].slot_state == SlotState::Visible) {
            ++snapshot.visible_operations;
        }
    }
    return snapshot;
}

void InstrumentStore::release_reservation(
    std::size_t slot,
    std::uint64_t generation) noexcept {
    if (slot < capacity_ && slots_[slot].slot_state == SlotState::Reserved &&
        slots_[slot].generation == generation) {
        slots_[slot] = Slot{};
    }
}

}  // namespace vna::store
