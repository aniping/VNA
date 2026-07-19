#include "runtime/platform/board/board_port.h"

#include "runtime/platform/board/acquisition_buffer_pool.h"

#include <array>
#include <cstddef>
#include <utility>

namespace vna::board {

AcquisitionChunkLease::AcquisitionChunkLease(
    AcquisitionBufferPool& owner,
    const ComplexSample* samples,
    std::size_t slot,
    std::uint64_t generation,
    std::size_t size) noexcept
    : owner_(&owner),
      samples_(samples),
      slot_(slot),
      generation_(generation),
      size_(size <= kMaximumContractChunkSamples ? size : 0U),
      valid_(samples != nullptr && size > 0U &&
             size <= kMaximumContractChunkSamples) {}

AcquisitionChunkLease::AcquisitionChunkLease(
    AcquisitionChunkLease&& other) noexcept
    : owner_(other.owner_),
      samples_(other.samples_),
      slot_(other.slot_),
      generation_(other.generation_),
      size_(other.size_),
      valid_(other.valid_) {
    other.invalidate();
}

AcquisitionChunkLease& AcquisitionChunkLease::operator=(
    AcquisitionChunkLease&& other) noexcept {
    if (this != &other) {
        release();
        owner_ = other.owner_;
        samples_ = other.samples_;
        slot_ = other.slot_;
        generation_ = other.generation_;
        size_ = other.size_;
        valid_ = other.valid_;
        other.invalidate();
    }
    return *this;
}

AcquisitionChunkLease::~AcquisitionChunkLease() {
    release();
}

void AcquisitionChunkLease::release() noexcept {
    if (valid_ && owner_ != nullptr) {
        owner_->release_lease(slot_, generation_);
    }
    invalidate();
}

void AcquisitionChunkLease::invalidate() noexcept {
    owner_ = nullptr;
    samples_ = nullptr;
    slot_ = 0U;
    generation_ = 0U;
    size_ = 0U;
    valid_ = false;
}

RunDeliveryGrant::RunDeliveryGrant(
    std::uint64_t grant_id,
    AcquisitionBufferPool& buffer_pool,
    std::array<std::size_t, kMaximumPreparedObservations> reserved_slots,
    std::array<std::uint64_t, kMaximumPreparedObservations> generations,
    std::size_t reserved_count) noexcept
    : grant_id_(grant_id),
      buffer_pool_(&buffer_pool),
      reserved_slots_(reserved_slots),
      generations_(generations),
      reserved_count_(reserved_count),
      valid_(grant_id != 0U && reserved_count > 0U &&
             reserved_count <= reserved_slots_.size()) {}

RunDeliveryGrant::RunDeliveryGrant(RunDeliveryGrant&& other) noexcept
    : grant_id_(other.grant_id_),
      buffer_pool_(other.buffer_pool_),
      reserved_slots_(other.reserved_slots_),
      generations_(other.generations_),
      reserved_count_(other.reserved_count_),
      issued_count_(other.issued_count_),
      valid_(other.valid_) {
    other.invalidate();
}

RunDeliveryGrant& RunDeliveryGrant::operator=(RunDeliveryGrant&& other) noexcept {
    if (this != &other) {
        retire();
        grant_id_ = other.grant_id_;
        buffer_pool_ = other.buffer_pool_;
        reserved_slots_ = other.reserved_slots_;
        generations_ = other.generations_;
        reserved_count_ = other.reserved_count_;
        issued_count_ = other.issued_count_;
        valid_ = other.valid_;
        other.invalidate();
    }
    return *this;
}

RunDeliveryGrant::~RunDeliveryGrant() {
    retire();
}

core::Result<AcquisitionChunkLease, BoardError>
RunDeliveryGrant::copy_fallback(
    const std::array<ComplexSample, kMaximumContractChunkSamples>& samples,
    std::size_t size) noexcept {
    if (!valid_ || buffer_pool_ == nullptr || issued_count_ >= reserved_count_) {
        return core::Result<AcquisitionChunkLease, BoardError>::failure(
            BoardError{BoardErrc::ResourceExhausted});
    }
    auto copied = buffer_pool_->copy_from_reserved(
        reserved_slots_[issued_count_],
        generations_[issued_count_],
        samples,
        size);
    if (copied.has_value()) {
        ++issued_count_;
    }
    return copied;
}

void RunDeliveryGrant::retire() noexcept {
    if (valid_ && buffer_pool_ != nullptr) {
        for (std::size_t index = issued_count_; index < reserved_count_; ++index) {
            buffer_pool_->release_reservation(
                reserved_slots_[index], generations_[index]);
        }
    }
    invalidate();
}

void RunDeliveryGrant::invalidate() noexcept {
    grant_id_ = 0U;
    buffer_pool_ = nullptr;
    reserved_slots_ = {};
    generations_ = {};
    reserved_count_ = 0U;
    issued_count_ = 0U;
    valid_ = false;
}

}  // namespace vna::board
