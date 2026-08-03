#include "sweep_runtime_internal.hpp"

#include <stdexcept>
#include <string>
#include <variant>

namespace vna::application::internal {

void SweepRuntimeImpl::requireTransition(
    OperationResult result,
    const char* transition) {
    if (std::holds_alternative<OperationError>(result)) {
        throw std::logic_error{
            std::string{"InternalInvariantViolation: "} + transition};
    }
}

std::exception_ptr SweepRuntimeImpl::cancelDetachedRequests(
    std::optional<OperationId> queued,
    std::optional<OperationId> active) noexcept {
    for (const auto operation : {queued, active}) {
        try {
            if (operation.has_value()) {
                requireTransition(
                    operations_.requestCancel(*operation), "requestCancel");
                requireTransition(
                    operations_.complete(*operation, OperationCanceled{}), "complete");
            }
        } catch (...) {
            std::lock_guard lock{mutex_};
            admissionClosed_ = true;
            return std::current_exception();
        }
    }
    return nullptr;
}

void SweepRuntimeImpl::settleTerminalFailure(
    OperationId operationId) noexcept {
    auto current = operations_.snapshot(operationId);
    const auto* snapshot = std::get_if<OperationSnapshot>(&current);
    if (snapshot == nullptr) {
        return;
    }
    if (std::holds_alternative<OperationRunning>(snapshot->state)) {
        static_cast<void>(operations_.complete(
            operationId, OperationFailed{{SingleSweepFailureCode::UnexpectedFailure}}));
        return;
    }
    if (std::holds_alternative<OperationQueued>(snapshot->state) ||
        std::holds_alternative<OperationCancelRequested>(snapshot->state)) {
        static_cast<void>(operations_.requestCancel(operationId));
        static_cast<void>(operations_.complete(operationId, OperationCanceled{}));
    }
}

}  // namespace vna::application::internal
