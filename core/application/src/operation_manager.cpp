#include <vna/application/operation_manager.hpp>

#include <optional>
#include <utility>

namespace vna::application {

OperationSnapshot OperationManager::create(OperationSubmission submission) {
    std::unique_lock lock{mutex_};
    std::optional<OperationId> committed;
    try {
        OperationSnapshot snapshot{
            .id = OperationId{nextOperationId_++},
            .commandId = std::move(submission.commandId),
            .sessionId = std::move(submission.sessionId),
            .submittedAtStateRevision = submission.submittedAtStateRevision,
            .state = OperationQueued{},
        };
        operations_.emplace(snapshot.id.value(), snapshot);
        committed = snapshot.id;
        return snapshot;
    } catch (...) {
        // Text identifiers deliberately preserve moved-from values, so return
        // construction may allocate. Keep the manager lock through rollback:
        // no fence can capture a Queued Operation that its caller never saw.
        if (committed) {
            operations_.erase(committed->value());
        }
        throw;
    }
}

OperationResult OperationManager::markRunning(OperationId operationId) {
    const std::scoped_lock lock{mutex_};
    const auto operation = operations_.find(operationId.value());
    if (operation == operations_.end()) {
        return OperationError{.code = OperationErrorCode::NotFound};
    }
    if (std::holds_alternative<OperationRunning>(operation->second.state)) {
        return operation->second;
    }
    if (!std::holds_alternative<OperationQueued>(operation->second.state)) {
        return OperationError{.code = OperationErrorCode::InvalidTransition};
    }
    operation->second.state = OperationRunning{};
    return operation->second;
}

OperationResult OperationManager::requestCancel(OperationId operationId) {
    const std::scoped_lock lock{mutex_};
    const auto operation = operations_.find(operationId.value());
    if (operation == operations_.end()) {
        return OperationError{.code = OperationErrorCode::NotFound};
    }
    if (std::holds_alternative<OperationCancelRequested>(
            operation->second.state)) {
        return operation->second;
    }
    const bool cancelable =
        std::holds_alternative<OperationQueued>(operation->second.state) ||
        std::holds_alternative<OperationRunning>(operation->second.state);
    if (!cancelable) {
        return OperationError{.code = OperationErrorCode::InvalidTransition};
    }
    operation->second.state = OperationCancelRequested{};
    return operation->second;
}

OperationResult OperationManager::snapshot(OperationId operationId) const {
    const std::scoped_lock lock{mutex_};
    const auto operation = operations_.find(operationId.value());
    if (operation == operations_.end()) {
        return OperationError{.code = OperationErrorCode::NotFound};
    }
    return operation->second;
}

}  // namespace vna::application
