#pragma once

#include <vna/application/command_bus.hpp>
#include <vna/application/single_sweep_command_handler.hpp>

namespace vna::test {

class StoppedSingleSweepExecution final
    : public application::SingleSweepExecution {
public:
    application::SingleSweepSubmitResult submit(
        application::SingleSweepWorkItem) override {
        return application::SingleSweepSubmitError{
            .code = application::SingleSweepSubmitErrorCode::Stopped};
    }

    void discardTrace(display_model::TraceId) noexcept override {}
};

// Existing command tests declare their intentionally inactive sweep boundary
// explicitly; production composition never receives this stopped handler.
inline application::SingleSweepCommandHandler& stoppedSingleSweepHandler() {
    static StoppedSingleSweepExecution execution;
    static application::SingleSweepCommandHandler handler{execution};
    return handler;
}

}  // namespace vna::test
