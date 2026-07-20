#include "runtime/function/acquisition/acquisition_drain_owner.h"

#include <utility>

namespace vna::acquisition {

AcquisitionDrainOwner::AcquisitionDrainOwner(
    runtime::DrainId drain,
    runtime::WorkId work,
    board::BoardRunId run,
    board::RunGeneration generation,
    AcquisitionIngress&& ingress,
    std::optional<board::PreparedManifestLease>&& prepared_manifest,
    std::optional<NetworkObservationBuilder>&& builder,
    AcquisitionAdmissionPool::Lease&& resources,
    board::BoardExecutionReservation&& board_reservation) noexcept
    : drain_(drain),
      work_(work),
      run_(run),
      generation_(generation),
      ingress_(std::move(ingress)),
      prepared_manifest_(std::move(prepared_manifest)),
      builder_(std::move(builder)),
      resources_(std::move(resources)),
      board_reservation_(std::move(board_reservation)) {}

bool AcquisitionDrainOwner::valid() const noexcept {
    return drain_.valid() && work_.valid() && ingress_.valid() &&
        resources_.valid() && board_reservation_.valid();
}

NetworkObservationBuilder* AcquisitionDrainOwner::builder() noexcept {
    return builder_.has_value() ? &*builder_ : nullptr;
}

AcquisitionDrainOwnershipSnapshot AcquisitionDrainOwner::inspect(
    bool runtime_completion_registered) const noexcept {
    const bool run_identity_valid = run_.valid() && generation_.valid();
    const bool exact_consumed =
        resources_.valid() && !resources_.owns_pre_dispatch_resources();
    return AcquisitionDrainOwnershipSnapshot{
        valid() && run_identity_valid && prepared_manifest_.has_value(),
        prepared_manifest_.has_value(),
        builder_.has_value(),
        valid(),
        runtime_completion_registered,
        resources_.valid(),
        resources_.valid(),
        exact_consumed,
        exact_consumed && run_identity_valid && prepared_manifest_.has_value()};
}

bool AcquisitionDrainOwner::finalize_failure() noexcept {
    return resources_.finalize_failure();
}

}  // namespace vna::acquisition
