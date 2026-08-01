#include "single_sweep_operation_internal.hpp"

#include <utility>

namespace vna::application::internal {

void cancelSingleSweepOperation(
    OperationManager& operations,
    OperationId operationId) {
    (void)operations.requestCancel(operationId);
    (void)operations.complete(operationId, OperationCanceled{});
}

void failSingleSweepOperation(
    OperationManager& operations,
    OperationId operationId,
    OperationFailure failure) {
    (void)operations.complete(
        operationId,
        OperationFailed{std::move(failure)});
}

bool singleSweepCancellationRequested(
    OperationManager& operations,
    OperationId operationId,
    std::stop_token token) {
    if (token.stop_requested()) {
        (void)operations.requestCancel(operationId);
    }
    const auto current = operations.snapshot(operationId);
    const auto* snapshot = std::get_if<OperationSnapshot>(&current);
    return snapshot != nullptr && std::holds_alternative<
                                      OperationCancelRequested>(
                                      snapshot->state);
}

bool finishSingleSweepCancellation(
    OperationManager& operations,
    OperationId operationId,
    std::stop_token token) {
    if (!singleSweepCancellationRequested(operations, operationId, token)) {
        return false;
    }
    (void)operations.complete(operationId, OperationCanceled{});
    return true;
}

}  // namespace vna::application::internal
