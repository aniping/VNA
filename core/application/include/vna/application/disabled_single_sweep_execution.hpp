#pragma once

#include <vna/application/single_sweep_executor.hpp>
#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::application {

// Production continuous acquisition owns the only RawSweepSource. This
// adapter keeps legacy command wiring honest without creating a second worker,
// Operation, queue, or source owner. The repository must outlive the adapter.
class DisabledSingleSweepExecution final : public SingleSweepExecution {
public:
    explicit DisabledSingleSweepExecution(
        TraceDisplayFrameRepository& repository) noexcept
        : repository_(repository) {}

    [[nodiscard]] SingleSweepSubmitResult submit(
        SingleSweepWorkItem) override {
        return SingleSweepSubmitError{SingleSweepSubmitErrorCode::Stopped};
    }

    void invalidateTraceFrame(
        display_model::TraceId traceId) noexcept override {
        repository_.discard(traceId);
    }

    void discardTrace(display_model::TraceId traceId) noexcept override {
        repository_.discard(traceId);
    }

private:
    TraceDisplayFrameRepository& repository_;
};

}  // namespace vna::application
