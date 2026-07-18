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
    const auto reject = [&](StoreErrc code) -> AcceptedCommitResult {
        return RejectedAcceptedCommit{
            StoreError{code}, std::move(reservation)};
    };

    if (!operation.valid()) {
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

    slot.slot_state = SlotState::Visible;
    slot.operation = OperationSnapshot{
        operation, OperationState::Accepted, ++revision_};
    reservation.invalidate();
    return AcceptedCommitReceipt{operation, revision_};
}

core::Result<TerminalCommitReceipt, StoreError> InstrumentStore::commit_terminal(
    OperationId operation,
    OperationState terminal_state) noexcept {
    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.slot_state != SlotState::Visible ||
            slot.operation.id != operation) {
            continue;
        }

        if (slot.operation.state != OperationState::Accepted) {
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

        slot.operation.state = terminal_state;
        slot.operation.revision = ++revision_;
        return core::Result<TerminalCommitReceipt, StoreError>::success(
            TerminalCommitReceipt{
                operation,
                terminal_state,
                revision_,
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

StoreSnapshot InstrumentStore::inspect() const noexcept {
    StoreSnapshot snapshot{};
    snapshot.revision = revision_;
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
