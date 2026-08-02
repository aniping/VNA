#include "sweep_runtime_internal.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace vna::application::internal {

SweepRuntimeRequestResult SweepRuntimeImpl::requestRestart(
    OperationSubmission submission) {
    std::optional<OperationId> queued;
    std::optional<OperationId> activeWithoutSource;
    std::optional<OperationId> activeWithSource;
    std::shared_ptr<std::stop_source> activeStop;
    std::optional<SweepPreviewIdentity> activeIdentity;
    OperationId createdId{0};
    {
        std::lock_guard lock{mutex_};
        if (snapshot_.state != SweepRuntimeState::Running) {
            const auto code = snapshot_.state == SweepRuntimeState::Stopped
                ? SweepRuntimeRequestErrorCode::Stopped
                : snapshot_.state == SweepRuntimeState::Retired
                ? SweepRuntimeRequestErrorCode::Retired
                : SweepRuntimeRequestErrorCode::Failed;
            return SweepRuntimeRequestError{code};
        }
        const auto created = operations_.create(std::move(submission));
        createdId = created.id;
        queued = std::exchange(pendingOperation_, createdId);
        activeStop = activeStop_;
        activeIdentity = activeIdentity_;
        if (activeRequest_.has_value()) {
            if (activeStop) {
                activeWithSource = activeRequest_->operationId;
            } else {
                activeWithoutSource = activeRequest_->operationId;
                activeRequest_.reset();
            }
        }
    }
    if (queued.has_value()) {
        cancelWithoutSource(*queued);
    }
    if (activeWithoutSource.has_value()) {
        cancelWithoutSource(*activeWithoutSource);
    }
    if (activeWithSource.has_value()) {
        static_cast<void>(operations_.requestCancel(*activeWithSource));
    }
    if (activeIdentity.has_value()) {
        invalidate(*activeIdentity);
    }
    if (activeStop) {
        activeStop->request_stop();
    }
    changed_.notify_all();
    return createdId;
}

void SweepRuntimeImpl::cancelWithoutSource(OperationId operationId) {
    static_cast<void>(operations_.requestCancel(operationId));
    static_cast<void>(operations_.complete(operationId, OperationCanceled{}));
}

bool SweepRuntimeImpl::prepareCycle(std::stop_token token) {
    std::optional<OperationId> operation;
    auto cycleStop = std::make_shared<std::stop_source>();
    {
        std::unique_lock lock{mutex_};
        if (!activeRequest_.has_value() &&
            plan_.execution.mode == domain::SweepMode::Single) {
            changed_.wait(lock, [&] {
                return token.stop_requested() || pendingOperation_.has_value();
            });
        }
        if (token.stop_requested()) {
            return false;
        }
        if (pendingOperation_.has_value()) {
            operation = pendingOperation_;
            pendingOperation_.reset();
            activeRequest_ = ActiveSweepRequest{
                *operation,
                plan_.execution.mode == domain::SweepMode::Single
                    ? plan_.execution.sweepCount
                    : 1};
        }
        snapshot_.phase = SweepRuntimePhase::Preparing;
        activeStop_ = cycleStop;
    }
    if (operation.has_value()) {
        const auto running = operations_.markRunning(*operation);
        if (std::holds_alternative<OperationError>(running)) {
            throw std::logic_error{"operation did not enter Running"};
        }
    }
    return true;
}

void SweepRuntimeImpl::cancelActiveAfterSource() {
    std::optional<OperationId> operation;
    {
        std::lock_guard lock{mutex_};
        activeStop_.reset();
        activeIdentity_.reset();
        if (activeRequest_.has_value()) {
            operation = activeRequest_->operationId;
            activeRequest_.reset();
        }
    }
    if (operation.has_value()) {
        cancelWithoutSource(*operation);
    }
}

void SweepRuntimeImpl::completeRequestedSweep(frames::FrameId frameId) {
    std::optional<OperationId> operation;
    {
        std::lock_guard lock{mutex_};
        if (activeRequest_.has_value() &&
            --activeRequest_->remainingSweeps == 0) {
            operation = activeRequest_->operationId;
            activeRequest_.reset();
            snapshot_.phase = plan_.execution.mode == domain::SweepMode::Single
                ? SweepRuntimePhase::Hold
                : SweepRuntimePhase::Preparing;
        }
    }
    if (operation.has_value()) {
        const auto completed = operations_.complete(
            *operation, OperationSucceeded{frameId});
        if (std::holds_alternative<OperationError>(completed)) {
            throw std::logic_error{"operation did not enter Succeeded"};
        }
    }
}

}  // namespace vna::application::internal
