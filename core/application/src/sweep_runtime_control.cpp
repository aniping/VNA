#include "sweep_runtime_internal.hpp"

#include <utility>
#include <variant>

namespace vna::application::internal {

bool SweepRuntimeImpl::prepareCycle(std::stop_token token) {
    auto cycleStop = std::make_shared<std::stop_source>();
    std::unique_lock lock{mutex_};
    applyPendingConfiguration();
    if (!activeRequest_.has_value() &&
        plan_.execution.mode == domain::SweepMode::Single) {
        if (snapshot_.phase != SweepUserPhase::Failed) {
            setDisplayStatusLocked(
                SweepUserPhase::Hold,
                std::nullopt,
                snapshot_.progress.totalPoints);
            previews_.updateForRuntime(displayStatusLocked());
        }
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
    activeStop_ = std::move(cycleStop);
    return true;
}

void SweepRuntimeImpl::cancelActiveAfterSource(
    SweepPreviewIdentity identity) {
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
        const auto hold = plan_.execution.mode == domain::SweepMode::Single &&
            !pendingOperation_.has_value();
        setDisplayStatusLocked(
            hold ? SweepUserPhase::Hold : SweepUserPhase::Preparing,
            std::nullopt,
            hold ? snapshot_.progress.totalPoints : 0);
        invalidateLocked(identity);
    }
    if (operation.has_value()) {
        requireTransition(
            operations_.complete(*operation, OperationCanceled{}),
            "complete");
    }
    changed_.notify_all();
}

void SweepRuntimeImpl::retireAfterSource(
    SweepPreviewIdentity identity) noexcept {
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
        setDisplayStatusLocked(
            SweepUserPhase::Hold,
            std::nullopt,
            snapshot_.progress.totalPoints);
        invalidateLocked(identity);
        changed_.notify_all();
    }
    if (auto failure = cancelDetachedRequests(queued, active)) {
        failTerminal(failure, queued, active);
    }
}

void SweepRuntimeImpl::completeRequestedSweep(
    SweepPreviewIdentity identity,
    frames::FrameId frameId) {
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
        }
        ++snapshot_.completedSweeps;
        snapshot_.firstSweepAfterConfiguration = false;
        const auto hold = plan_.execution.mode == domain::SweepMode::Single &&
            !activeRequest_.has_value();
        setDisplayStatusLocked(
            hold ? SweepUserPhase::Hold : SweepUserPhase::Preparing,
            std::nullopt,
            hold ? snapshot_.progress.totalPoints : 0);
        invalidateLocked(identity);
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
