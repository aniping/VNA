#include "runtime/platform/board/board_prepare_drain_owner.h"

namespace vna::board {

BoardPrepareDrainOwner BoardPrepareDrainOwner::issue_for_adapter() noexcept {
    return BoardPrepareDrainOwner{true};
}

BoardPrepareDrainOwner::BoardPrepareDrainOwner(
    BoardPrepareDrainOwner&& other) noexcept
    : valid_(other.valid_) {
    other.valid_ = false;
}

}  // namespace vna::board
