#pragma once

#include <optional>

#include <vna/application/single_sweep_executor.hpp>

namespace vna::application::internal {

// Invocation is isolated because repository allocation is an external
// exception boundary. Success means the immutable frame is already visible;
// failure remains a value that the executor can commit as OperationFailed.
[[nodiscard]] std::optional<OperationFailure> publishTraceDisplayFrame(
    const TraceDisplayPublisher& publish,
    TraceDisplayFrame frame) noexcept;

}  // namespace vna::application::internal
