#include "runtime/platform/board/acquisition_buffer_pool.h"

#include <algorithm>
#include <utility>

namespace vna::board {

AcquisitionBufferPool::AcquisitionBufferPool(std::size_t capacity) noexcept
    : capacity_(capacity > 0U && capacity <= slots_.size() ? capacity : 0U) {}

core::Result<RunDeliveryGrant, AcquisitionBufferPoolError>
AcquisitionBufferPool::reserve_delivery(
    std::uint64_t grant_id,
    std::size_t buffer_count) noexcept {
    const auto fail = [](AcquisitionBufferPoolErrc code) noexcept {
        return core::Result<RunDeliveryGrant, AcquisitionBufferPoolError>::failure(
            AcquisitionBufferPoolError{code});
    };
    if (grant_id == 0U || capacity_ == 0U || buffer_count == 0U ||
        buffer_count > capacity_) {
        return fail(AcquisitionBufferPoolErrc::InvalidReservation);
    }

    std::size_t free_count{0U};
    for (std::size_t index = 0U; index < capacity_; ++index) {
        if (slots_[index].state == SlotState::Free) {
            ++free_count;
        }
    }
    if (free_count < buffer_count) {
        return fail(AcquisitionBufferPoolErrc::ResourceExhausted);
    }

    std::array<std::size_t, kMaximumPreparedObservations> indices{};
    std::array<std::uint64_t, kMaximumPreparedObservations> generations{};
    std::size_t selected{0U};
    for (std::size_t index = 0U;
         index < capacity_ && selected < buffer_count;
         ++index) {
        auto& slot = slots_[index];
        if (slot.state != SlotState::Free) {
            continue;
        }
        ++slot.generation;
        if (slot.generation == 0U) {
            ++slot.generation;
        }
        slot.size = 0U;
        slot.state = SlotState::Reserved;
        indices[selected] = index;
        generations[selected] = slot.generation;
        ++selected;
    }

    return core::Result<RunDeliveryGrant, AcquisitionBufferPoolError>::success(
        RunDeliveryGrant{
            grant_id, *this, indices, generations, buffer_count});
}

AcquisitionBufferPool::Snapshot AcquisitionBufferPool::inspect() const noexcept {
    Snapshot snapshot{};
    snapshot.capacity = capacity_;
    snapshot.copy_operations = copy_operations_;
    for (std::size_t index = 0U; index < capacity_; ++index) {
        if (slots_[index].state == SlotState::Reserved) {
            ++snapshot.reserved;
        } else if (slots_[index].state == SlotState::Leased) {
            ++snapshot.leased;
        }
    }
    return snapshot;
}

core::Result<AcquisitionChunkLease, BoardError>
AcquisitionBufferPool::copy_from_reserved(
    std::size_t slot_index,
    std::uint64_t generation,
    const std::array<ComplexSample, kMaximumContractChunkSamples>& samples,
    std::size_t size) noexcept {
    if (slot_index >= capacity_ || size == 0U ||
        size > kMaximumContractChunkSamples) {
        return core::Result<AcquisitionChunkLease, BoardError>::failure(
            BoardError{BoardErrc::ContractViolation});
    }
    auto& slot = slots_[slot_index];
    if (slot.state != SlotState::Reserved || slot.generation != generation) {
        return core::Result<AcquisitionChunkLease, BoardError>::failure(
            BoardError{BoardErrc::ContractViolation});
    }

    // 这是不可转移底软内存的唯一复制点；lease 后续移动只携带槽位身份和指针。
    std::copy_n(samples.begin(), size, slot.samples.begin());
    slot.size = size;
    slot.state = SlotState::Leased;
    ++copy_operations_;
    return core::Result<AcquisitionChunkLease, BoardError>::success(
        AcquisitionChunkLease{
            *this,
            slot.samples.data(),
            slot_index,
            generation,
            size});
}

void AcquisitionBufferPool::release_reservation(
    std::size_t slot_index,
    std::uint64_t generation) noexcept {
    if (slot_index >= capacity_) {
        return;
    }
    auto& slot = slots_[slot_index];
    if (slot.state == SlotState::Reserved && slot.generation == generation) {
        slot.size = 0U;
        slot.state = SlotState::Free;
    }
}

void AcquisitionBufferPool::release_lease(
    std::size_t slot_index,
    std::uint64_t generation) noexcept {
    if (slot_index >= capacity_) {
        return;
    }
    auto& slot = slots_[slot_index];
    if (slot.state == SlotState::Leased && slot.generation == generation) {
        slot.size = 0U;
        slot.state = SlotState::Free;
    }
}

}  // namespace vna::board
