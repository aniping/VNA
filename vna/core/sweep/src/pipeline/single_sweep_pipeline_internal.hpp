#pragma once

#include <functional>
#include <vna/compat/stop_token.hpp>
#include <variant>

#include <vna/application/single_sweep_executor.hpp>

namespace vna::application::internal {

struct SweepPipelineCanceled {};

using SweepPipelineResult = std::variant<
    TraceDisplayFrame,
    OperationFailure,
    SweepPipelineCanceled>;
using SweepCancellationCheck = std::function<bool()>;

// This private seam owns transformation order only. Queue admission, Operation
// transitions, and the repository commit remain executor responsibilities.
[[nodiscard]] SweepPipelineResult buildSingleSweepFrame(
    const SingleSweepWorkItem& work,
    const RawSweepSource& source,
    vna::compat::StopToken token,
    const SweepCancellationCheck& canceled);

}  // namespace vna::application::internal
