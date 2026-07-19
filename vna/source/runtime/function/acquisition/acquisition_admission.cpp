#include "runtime/function/acquisition/acquisition_admission.h"

#include <algorithm>

namespace vna::acquisition {

AcquisitionAdmissionPool::LocalResourceOwner::LocalResourceOwner(
    AcquisitionAdmissionPool& owner,
    std::size_t slot,
    std::uint64_t generation,
    LocalResource resource) noexcept
    : owner_(&owner),
      slot_(slot),
      generation_(generation),
      resource_(resource) {}

AcquisitionAdmissionPool::LocalResourceOwner::LocalResourceOwner(
    LocalResourceOwner&& other) noexcept
    : owner_(other.owner_),
      slot_(other.slot_),
      generation_(other.generation_),
      resource_(other.resource_) {
    other.invalidate();
}

AcquisitionAdmissionPool::LocalResourceOwner&
AcquisitionAdmissionPool::LocalResourceOwner::operator=(
    LocalResourceOwner&& other) noexcept {
    if (this != &other) {
        release();
        owner_ = other.owner_;
        slot_ = other.slot_;
        generation_ = other.generation_;
        resource_ = other.resource_;
        other.invalidate();
    }
    return *this;
}

AcquisitionAdmissionPool::LocalResourceOwner::~LocalResourceOwner() {
    release();
}

bool AcquisitionAdmissionPool::LocalResourceOwner::valid() const noexcept {
    return owner_ != nullptr;
}

bool AcquisitionAdmissionPool::LocalResourceOwner::retire() noexcept {
    if (!valid()) {
        return false;
    }
    release();
    return true;
}

void AcquisitionAdmissionPool::LocalResourceOwner::release() noexcept {
    if (owner_ != nullptr) {
        owner_->release_resource(slot_, generation_, resource_);
        invalidate();
    }
}

void AcquisitionAdmissionPool::LocalResourceOwner::invalidate() noexcept {
    owner_ = nullptr;
    slot_ = 0U;
    generation_ = 0U;
    resource_ = LocalResource::AOutput;
}

AcquisitionAdmissionPool::Lease::Lease(
    AcquisitionAdmissionPool& owner,
    std::size_t slot,
    std::uint64_t generation,
    Claim claim) noexcept
    : owner_(&owner),
      claim_(claim),
      a_output_(owner, slot, generation),
      candidate_metadata_(owner, slot, generation),
      buffer_(owner, slot, generation),
      ingress_(owner, slot, generation),
      a_only_completion_(owner, slot, generation),
      disabled_preview_(owner, slot, generation),
      exact_finalization_(owner, slot, generation) {}

bool AcquisitionAdmissionPool::Lease::valid() const noexcept {
    return a_output_.valid() && candidate_metadata_.valid() && buffer_.valid() &&
        ingress_.valid() && a_only_completion_.valid() &&
        disabled_preview_.valid();
}

bool AcquisitionAdmissionPool::Lease::owns_pre_dispatch_resources() const noexcept {
    return valid() && exact_finalization_.valid();
}

bool AcquisitionAdmissionPool::Lease::narrow_to(
    const board::PreparedExecutionManifest& manifest) noexcept {
    if (!valid() || !exact_finalization_.retire()) {
        return false;
    }

    const auto& capabilities = claim_.capabilities;
    return manifest.id.valid() && manifest.prepared_id.valid() &&
        manifest.manifest_digest.valid() &&
        manifest.session_id == capabilities.session_id &&
        manifest.session_epoch == capabilities.session_epoch &&
        manifest.capability_revision == capabilities.capability_revision &&
        manifest.topology_epoch == capabilities.topology_epoch &&
        manifest.operational_epoch == capabilities.operational_epoch &&
        manifest.intent_digest == claim_.plan_digest &&
        manifest.actual_point_count > 0U &&
        manifest.actual_point_count <= claim_.maximum_points &&
        manifest.actual_start_hz >= claim_.minimum_start_hz &&
        manifest.actual_stop_hz <= claim_.maximum_stop_hz &&
        (manifest.actual_point_count == 1U
             ? manifest.actual_stop_hz == manifest.actual_start_hz
             : manifest.actual_stop_hz > manifest.actual_start_hz);
}

bool AcquisitionAdmissionPool::Lease::finalize_failure() noexcept {
    if (!valid()) {
        return false;
    }
    const auto completion_retired = a_only_completion_.retire();
    const auto preview_retired = disabled_preview_.retire();
    if (!completion_retired || !preview_retired) {
        return false;
    }
    (void)exact_finalization_.retire();
    // 两个 purpose-specific owner 都终结后才记录一次，避免布尔计数冒充部分完成。
    owner_->record_failure_finalization();
    return true;
}

AcquisitionAdmissionPool::AcquisitionAdmissionPool(std::size_t capacity) noexcept
    : capacity_(std::min(capacity, kMaximumLeases)) {}

core::Result<AcquisitionAdmissionPool::Lease, AcquisitionAdmissionPool::Error>
AcquisitionAdmissionPool::reserve(Claim claim) noexcept {
    const auto& capabilities = claim.capabilities;
    const bool valid = claim.plan_digest.valid() &&
        capabilities.session_id.valid() &&
        capabilities.capability_revision != 0U &&
        capabilities.topology_epoch != 0U &&
        capabilities.operational_epoch != 0U &&
        claim.maximum_points > 0U &&
        claim.maximum_points <= capabilities.maximum_points &&
        claim.maximum_chunks > 0U && claim.minimum_start_hz > 0.0 &&
        claim.maximum_stop_hz >= claim.minimum_start_hz;
    if (!valid) {
        return core::Result<Lease, Error>::failure(Error{Errc::InvalidClaim});
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.resources != 0U) {
            continue;
        }
        slot.generation = next_generation_++;
        slot.resources = kAllLocalResources;
        return core::Result<Lease, Error>::success(
            Lease{*this, index, slot.generation, claim});
    }
    return core::Result<Lease, Error>::failure(Error{Errc::ResourceExhausted});
}

AcquisitionAdmissionPool::Snapshot AcquisitionAdmissionPool::inspect() const noexcept {
    Snapshot snapshot{};
    snapshot.failure_finalizations = failure_finalizations_;
    for (std::size_t index = 0U; index < capacity_; ++index) {
        if (slots_[index].resources != 0U) {
            ++snapshot.in_use;
        }
    }
    return snapshot;
}

void AcquisitionAdmissionPool::release_resource(
    std::size_t slot,
    std::uint64_t generation,
    LocalResource resource) noexcept {
    if (slot < capacity_ && slots_[slot].generation == generation) {
        slots_[slot].resources &= static_cast<ResourceMask>(~mask(resource));
        if (slots_[slot].resources == 0U) {
            slots_[slot] = Slot{};
        }
    }
}

void AcquisitionAdmissionPool::record_failure_finalization() noexcept {
    ++failure_finalizations_;
}

}  // namespace vna::acquisition
