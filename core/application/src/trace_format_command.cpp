#include <vna/application/command_bus.hpp>

#include <algorithm>

#include <vna/application/single_sweep_command_handler.hpp>

namespace vna::application {
namespace {

bool leavesLogMagnitude(
    const display_model::DisplayWorkspaceSnapshot& snapshot,
    const UpdateTraceFormatCommand& command) {
    return command.format != display_model::TraceFormat::LogMagnitude &&
        std::any_of(
            snapshot.traces.cbegin(), snapshot.traces.cend(),
            [&](const display_model::TraceSnapshot& trace) {
                return trace.id == command.traceId &&
                    trace.format == display_model::TraceFormat::LogMagnitude;
            });
}

}  // namespace

CommandResult CommandBus::execute(const UpdateTraceFormatCommand& command) {
    const auto invalidatesFrame =
        leavesLogMagnitude(displayWorkspace_.snapshot(), command);
    const auto trace =
        displayWorkspace_.updateTraceFormat(command.traceId, command.format);
    if (!trace.hasValue()) {
        return displayError(trace.error());
    }
    if (invalidatesFrame) {
        singleSweepHandler_.invalidateFrame(command.traceId);
    }
    return succeeded(CommandValue{trace.value()});
}

}  // namespace vna::application
