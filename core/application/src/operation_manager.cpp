#include <vna/application/operation_manager.hpp>

#include <utility>

namespace vna::application {

OperationSnapshot OperationManager::create(OperationSubmission submission) {
    const std::scoped_lock lock{mutex_};
    const OperationSnapshot snapshot{
        .id = OperationId{nextOperationId_++},
        .commandId = std::move(submission.commandId),
        .sessionId = std::move(submission.sessionId),
        .submittedAtStateRevision = submission.submittedAtStateRevision,
        .state = OperationQueued{},
    };
    operations_.emplace(snapshot.id.value(), snapshot);
    return snapshot;
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
