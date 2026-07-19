#include "runtime/platform/board/board_execution_reservation.h"

#include "runtime/platform/board/board_port.h"

namespace vna::board {

BoardExecutionReservation::BoardExecutionReservation(
    BoardExecutionPort& owner,
    BoardExecutionReservationId id) noexcept
    : owner_(&owner), id_(id) {}

BoardExecutionReservation::BoardExecutionReservation(
    BoardExecutionReservation&& other) noexcept
    : owner_(other.owner_), id_(other.id_) {
    other.invalidate();
}

BoardExecutionReservation::~BoardExecutionReservation() {
    release();
}

bool BoardExecutionReservation::valid() const noexcept {
    return owner_ != nullptr && id_.valid();
}

void BoardExecutionReservation::release() noexcept {
    if (owner_ != nullptr) {
        owner_->release_execution_reservation(id_);
        invalidate();
    }
}

void BoardExecutionReservation::invalidate() noexcept {
    owner_ = nullptr;
    id_ = BoardExecutionReservationId{};
}

}  // namespace vna::board
