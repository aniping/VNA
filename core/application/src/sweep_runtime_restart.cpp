#include "sweep_runtime_internal.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace vna::application::internal {

SweepRuntimeAdmissionResult SweepRuntimeImpl::admitRestart(
    domain::ChannelId channelId,
    OperationSubmission submission) {
    // Allocate the opaque settlement before Operation creation. Everything
    // after create is fixed-size, non-throwing installation.
    auto state = std::make_unique<detail::RestartAdmissionState>();
    auto prepared = prepareRestart(channelId, std::move(submission));
    if (const auto* error =
            std::get_if<SweepRuntimeRequestError>(&prepared)) {
        return *error;
    }
    static_assert(std::is_nothrow_move_constructible_v<RestartAdmissionData>);
    state->owner = this;
    state->admission.emplace(
        std::get<RestartAdmissionData>(std::move(prepared)));
    return vna::application::RestartAdmission{std::move(state)};
}

void SweepRuntimeImpl::settleRestart(
    RestartAdmissionData admission) noexcept {
    auto failure = admission.invariant;
    if (failure == nullptr) {
        failure = cancelDetachedRequests(
            admission.queued, admission.activeWithoutSource);
    }
    if (failure != nullptr) {
        if (admission.activeStop) {
            admission.activeStop->request_stop();
        }
        worker_.request_stop();
        notifyWorker();
        failTerminal(
            failure, admission.queued, admission.activeWithoutSource);
        return;
    }
    if (admission.activeIdentity.has_value()) {
        std::lock_guard lock{mutex_};
        setDisplayStatusLocked(
            SweepUserPhase::Preparing,
            std::nullopt,
            0);
        invalidateLocked(*admission.activeIdentity);
        activeIdentity_.reset();
    }
    if (admission.activeStop) {
        admission.activeStop->request_stop();
    }
    changed_.notify_all();
}

RestartAdmissionResult SweepRuntimeImpl::prepareRestart(
    domain::ChannelId channelId,
    OperationSubmission submission) {
    std::unique_lock lock{mutex_};
    // A publication that already claimed its boundary may finish first; the
    // replacement remains a bounded admission and begins at the next boundary.
    if (channelId != plan_.publication->channelId) {
        return SweepRuntimeRequestError{
            SweepRuntimeRequestErrorCode::UnsupportedChannel};
    }
    if (snapshot_.state != SweepRuntimeState::Running || admissionClosed_) {
        const auto code = snapshot_.state == SweepRuntimeState::Stopped
            ? SweepRuntimeRequestErrorCode::Stopped
            : snapshot_.state == SweepRuntimeState::Retired
            ? SweepRuntimeRequestErrorCode::Retired
            : SweepRuntimeRequestErrorCode::Failed;
        return SweepRuntimeRequestError{code};
    }
    const auto invariant = std::make_exception_ptr(
        std::logic_error{"InternalInvariantViolation: requestCancel"});
    const auto created = operations_.create(std::move(submission));
    auto result = RestartAdmissionData{.createdId = created.id};
    result.queued = std::exchange(pendingOperation_, result.createdId);
    result.activeStop = activeStop_;
    result.activeIdentity = activeIdentity_;
    if (activeRequest_.has_value() && result.activeStop) {
        try {
            const auto canceled = operations_.requestCancel(
                activeRequest_->operationId);
            if (std::holds_alternative<OperationError>(canceled)) {
                result.invariant = invariant;
            }
        } catch (...) {
            result.invariant = invariant;
        }
    }
    if (result.invariant != nullptr) {
        admissionClosed_ = true;
        cycleCancellationRequested_ = true;
        return result;
    }
    if (activeRequest_.has_value() && !result.activeStop) {
        result.activeWithoutSource = activeRequest_->operationId;
        activeRequest_.reset();
    }
    cycleCancellationRequested_ = result.activeStop != nullptr;
    return result;
}

}  // namespace vna::application::internal
