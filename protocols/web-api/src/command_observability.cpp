#include "command_observability.hpp"

#include <string>
#include <variant>

namespace vna::web_api::detail {
namespace {

struct CommandEventName {
    const char* operator()(const application::CreateChannelCommand&) const {
        return "web.command.create_channel";
    }
    const char* operator()(const application::UpdateChannelSweepCommand&) const {
        return "web.command.update_channel_sweep";
    }
    const char* operator()(const application::CreateMeasurementCommand&) const {
        return "web.command.create_measurement";
    }
    const char* operator()(const application::CreateWindowCommand&) const {
        return "web.command.create_window";
    }
    const char* operator()(const application::CreateTraceCommand&) const {
        return "web.command.create_trace";
    }
    const char* operator()(const application::UpdateTraceFormatCommand&) const {
        return "web.command.update_trace_format";
    }
    const char* operator()(
        const application::SetTraceMeasurementTypeCommand&) const {
        return "web.command.set_trace_measurement_type";
    }
    const char* operator()(
        const application::UpdateTraceScalePerDivisionCommand&) const {
        return "web.command.update_trace_scale_per_division";
    }
    const char* operator()(const application::RemoveTraceCommand&) const {
        return "web.command.remove_trace";
    }
    const char* operator()(const application::StartSingleSweepCommand&) const {
        return "web.command.start_single_sweep";
    }
};

}  // namespace

bool recordWebCommand(
    observability::Logger* logger,
    const application::CommandEnvelope& command,
    const application::CommandResult& result) noexcept {
    if (logger == nullptr) return true;
    const bool succeeded =
        std::holds_alternative<application::CommandSuccess>(result.outcome);
    try {
        const auto written = logger->write({
            .level = succeeded ? observability::LogLevel::Info
                               : observability::LogLevel::Warning,
            .name = std::visit(CommandEventName{}, command.payload),
            .commandId = command.commandId.value(),
            .sessionId = command.sessionId.value(),
            .instrumentId = command.instrumentId.value(),
            .stateRevision = result.stateRevision,
            .status = succeeded ? "succeeded" : "rejected",
        });
        // Browser commands are low-rate control events. Flushing here makes
        // the authoritative audit trail visible during a long-running server.
        const auto flushed = logger->flush();
        return written && flushed;
    } catch (...) {
        // Allocation while shaping diagnostics must not rewrite HTTP semantics.
        return false;
    }
}

}  // namespace vna::web_api::detail
