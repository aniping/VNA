#pragma once

#include <vna/application/single_sweep_executor.hpp>

namespace vna::application {

// Production continuous acquisition owns the only RawSweepSource. This
// adapter keeps legacy command wiring honest without creating a second worker,
// Operation, queue, source owner, or competing display invalidation path.
class DisabledSingleSweepExecution final : public SingleSweepExecution {
public:
    [[nodiscard]] SingleSweepSubmitResult submit(
        SingleSweepWorkItem) override {
        return SingleSweepSubmitError{SingleSweepSubmitErrorCode::Stopped};
    }

    void invalidateTraceFrame(
        display_model::TraceId) noexcept override {}

    void discardTrace(display_model::TraceId) noexcept override {}
};

}  // namespace vna::application
