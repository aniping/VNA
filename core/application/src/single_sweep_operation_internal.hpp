#pragma once

#include <stop_token>

#include <vna/application/operation_manager.hpp>

namespace vna::application::internal {

// These transitions are kept outside the queue worker so its synchronization
// remains focused on ownership and scheduling rather than Operation policy.
void cancelSingleSweepOperation(
    OperationManager& operations,
    OperationId operationId);
void failSingleSweepOperation(
    OperationManager& operations,
    OperationId operationId,
    OperationFailure failure);
[[nodiscard]] bool singleSweepCancellationRequested(
    OperationManager& operations,
    OperationId operationId,
    std::stop_token token);
[[nodiscard]] bool finishSingleSweepCancellation(
    OperationManager& operations,
    OperationId operationId,
    std::stop_token token);

}  // namespace vna::application::internal
