#pragma once

#include <vna/application/command_bus.hpp>
#include <vna/application/single_sweep_command_handler.hpp>

namespace vna::test {

// Existing command tests declare their intentionally inactive sweep boundary
// explicitly; production composition never receives this stopped handler.
inline application::SingleSweepCommandHandler& stoppedSingleSweepHandler() {
    static application::SingleSweepCommandHandler handler{
        [](application::SingleSweepWorkItem)
            -> application::SingleSweepSubmitResult {
            return application::SingleSweepSubmitError{
                .code = application::SingleSweepSubmitErrorCode::Stopped};
        }};
    return handler;
}

}  // namespace vna::test
