#pragma once

#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/application/single_sweep_executor.hpp>

namespace vna::application {

// Production continuous acquisition owns the only RawSweepSource. This
// adapter keeps legacy command wiring honest without creating a second worker,
// Operation, queue, or source owner. The publisher must outlive the adapter.
class DisabledSingleSweepExecution final : public SingleSweepExecution {
public:
    explicit DisabledSingleSweepExecution(
        ContinuousTracePublisher& publisher) noexcept
        : publisher_(publisher) {}

    [[nodiscard]] SingleSweepSubmitResult submit(
        SingleSweepWorkItem) override {
        return SingleSweepSubmitError{SingleSweepSubmitErrorCode::Stopped};
    }

    void discardTrace(display_model::TraceId traceId) noexcept override {
        publisher_.retireTrace(traceId);
    }

private:
    ContinuousTracePublisher& publisher_;
};

}  // namespace vna::application
