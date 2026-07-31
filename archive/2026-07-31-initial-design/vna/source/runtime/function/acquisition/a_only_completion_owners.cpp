#include "runtime/function/acquisition/a_only_completion_owners.h"

#include <utility>

namespace vna::acquisition {

AOnlyCompletionOwners::AOnlyCompletionOwners(
    AcquisitionAdmissionPool::Lease&& resources,
    CompletedSweepId expected_snapshot) noexcept
    : resources_(std::move(resources)), expected_snapshot_(expected_snapshot) {}

AOnlyCompletionOwners::AOnlyCompletionOwners(
    AOnlyCompletionOwners&& other) noexcept
    : resources_(std::move(other.resources_)),
      expected_snapshot_(other.expected_snapshot_) {
    other.resources_.reset();
    other.expected_snapshot_ = CompletedSweepId{};
}

bool AOnlyCompletionOwners::valid() const noexcept {
    return resources_.has_value() && resources_->valid() &&
        expected_snapshot_.valid();
}

bool AOnlyCompletionOwners::finalize_published(
    CompletedSweepId published_snapshot) noexcept {
    if (!valid() || published_snapshot != expected_snapshot_) {
        return false;
    }
    const auto finalized = resources_->finalize_success();
    if (finalized) {
        resources_.reset();
        expected_snapshot_ = CompletedSweepId{};
    }
    return finalized;
}

bool AOnlyCompletionOwners::finalize_failed() noexcept {
    if (!valid()) {
        return false;
    }
    const auto finalized = resources_->finalize_failure();
    if (finalized) {
        resources_.reset();
        expected_snapshot_ = CompletedSweepId{};
    }
    return finalized;
}

}  // namespace vna::acquisition
