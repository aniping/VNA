#include "sweep_runtime_internal.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace vna::application::internal {

SweepRuntimeRequestResult SweepRuntimeImpl::requestRestart(
    domain::ChannelId channelId,
    OperationSubmission submission) {
    auto admitted = admitRestart(channelId, std::move(submission));
    if (const auto* error =
            std::get_if<SweepRuntimeRequestError>(&admitted)) {
        return *error;
    }
    auto admission = std::get<RestartAdmission>(std::move(admitted));
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
        return admission.createdId;
    }
    if (admission.activeIdentity.has_value()) {
        invalidate(*admission.activeIdentity);
    }
    if (admission.activeStop) {
        admission.activeStop->request_stop();
    }
    changed_.notify_all();
    return admission.createdId;
}

RestartAdmissionResult SweepRuntimeImpl::admitRestart(
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
    const auto cancellationInvariant = std::make_exception_ptr(
        std::logic_error{"InternalInvariantViolation: requestCancel"});
    const auto created = operations_.create(std::move(submission));
    auto result = RestartAdmission{.createdId = created.id};
    result.queued = std::exchange(pendingOperation_, result.createdId);
    result.activeStop = activeStop_;
    result.activeIdentity = activeIdentity_;
    if (activeRequest_.has_value() && result.activeStop) {
        try {
            auto canceled = operations_.requestCancel(
                activeRequest_->operationId);
            if (std::holds_alternative<OperationError>(canceled)) {
                result.invariant = cancellationInvariant;
            }
        } catch (...) {
            result.invariant = cancellationInvariant;
        }
        if (result.invariant != nullptr) {
            admissionClosed_ = true;
            cycleCancellationRequested_ = true;
            return result;
        }
    }
    if (activeRequest_.has_value() && !result.activeStop) {
        result.activeWithoutSource = activeRequest_->operationId;
        activeRequest_.reset();
    }
    cycleCancellationRequested_ = result.activeStop != nullptr;
    return result;
}

bool SweepRuntimeImpl::prepareCycle(std::stop_token token) {
    auto cycleStop = std::make_shared<std::stop_source>();
    std::unique_lock lock{mutex_};
    applyPendingConfiguration();
    if (!activeRequest_.has_value() &&
        plan_.execution.mode == domain::SweepMode::Single) {
        snapshot_.phase = SweepRuntimePhase::Hold;
        changed_.wait(lock, [&] {
            return token.stop_requested() || pendingOperation_.has_value() ||
                plan_.execution.mode == domain::SweepMode::Continuous;
        });
    }
    if (token.stop_requested()) {
        return false;
    }
    if (pendingOperation_.has_value()) {
        const auto operation = *pendingOperation_;
        // markRunning has no callbacks; keeping the runtime gate prevents a
        // replacement from observing a half-promoted request.
        requireTransition(operations_.markRunning(operation), "markRunning");
        pendingOperation_.reset();
        activeRequest_ = ActiveSweepRequest{
            operation,
            plan_.execution.mode == domain::SweepMode::Single
                ? plan_.execution.sweepCount
                : 1};
    }
    snapshot_.phase = SweepRuntimePhase::Preparing;
    activeStop_ = std::move(cycleStop);
    return true;
}

void SweepRuntimeImpl::cancelActiveAfterSource() {
    std::optional<OperationId> operation;
    {
        std::lock_guard lock{mutex_};
        activeStop_.reset();
        activeIdentity_.reset();
        cycleCancellationRequested_ = false;
        if (activeRequest_.has_value()) {
            operation = activeRequest_->operationId;
            activeRequest_.reset();
        }
    }
    if (operation.has_value()) {
        requireTransition(
            operations_.complete(*operation, OperationCanceled{}),
            "complete");
    }
    changed_.notify_all();
}

void SweepRuntimeImpl::retireAfterSource() noexcept {
    std::optional<OperationId> queued, active;
    {
        std::lock_guard lock{mutex_};
        snapshot_.state = SweepRuntimeState::Retired;
        finalizingPublication_ = false;
        cycleCancellationRequested_ = false;
        queued = std::exchange(pendingOperation_, std::nullopt);
        activeStop_.reset();
        activeIdentity_.reset();
        if (activeRequest_.has_value()) {
            active = activeRequest_->operationId;
            activeRequest_.reset();
        }
        changed_.notify_all();
    }
    if (auto failure = cancelDetachedRequests(queued, active)) {
        failTerminal(failure, queued, active);
    }
}

void SweepRuntimeImpl::completeRequestedSweep(frames::FrameId frameId) {
    std::optional<OperationId> operation;
    {
        std::lock_guard lock{mutex_};
        activeStop_.reset();
        activeIdentity_.reset();
        finalizingPublication_ = false;
        cycleCancellationRequested_ = false;
        if (activeRequest_.has_value() &&
            --activeRequest_->remainingSweeps == 0) {
            operation = activeRequest_->operationId;
            activeRequest_.reset();
            snapshot_.phase = plan_.execution.mode == domain::SweepMode::Single
                ? SweepRuntimePhase::Hold
                : SweepRuntimePhase::Preparing;
        }
        changed_.notify_all();
    }
    if (operation.has_value()) {
        requireTransition(
            operations_.complete(*operation, OperationSucceeded{frameId}),
            "complete");
    }
}

void SweepRuntimeImpl::failRequestedSweep(
    const SweepRuntimeFailure& failure) {
    std::optional<OperationId> operation;
    bool canceled{};
    {
        std::lock_guard lock{mutex_};
        activeStop_.reset();
        activeIdentity_.reset();
        canceled = cycleCancellationRequested_;
        finalizingPublication_ = false;
        cycleCancellationRequested_ = false;
        if (activeRequest_.has_value()) {
            operation = activeRequest_->operationId;
            activeRequest_.reset();
            snapshot_.phase = plan_.execution.mode == domain::SweepMode::Single
                ? SweepRuntimePhase::Hold
                : SweepRuntimePhase::Preparing;
        }
        changed_.notify_all();
    }
    if (!operation.has_value()) {
        return;
    }
    if (canceled) {
        requireTransition(
            operations_.complete(*operation, OperationCanceled{}),
            "complete");
        return;
    }
    auto cause = OperationFailureCause{};
    if (const auto* frame = std::get_if<frames::FrameError>(&failure.cause)) {
        cause = *frame;
    }
    const auto code = failure.code == SweepRuntimeFailureCode::CaptureFailed
        ? SingleSweepFailureCode::RawSweepFailed
        : failure.code == SweepRuntimeFailureCode::PublicationRejected
        ? SingleSweepFailureCode::TraceDisplayPublishFailed
        : SingleSweepFailureCode::UnexpectedFailure;
    requireTransition(
        operations_.complete(
            *operation, OperationFailed{{code, std::move(cause)}}),
        "complete");
}

}  // namespace vna::application::internal
