#include "command_observability.hpp"
#include "command_outcome_info.hpp"

#include <optional>
#include <string>
#include <variant>

namespace vna::web_api::detail {
namespace {

struct CommandDescription {
    const char* event;
    const char* action;
};

struct DescribeCommand {
    CommandDescription operator()(const application::CreateChannelCommand&) const {
        return {"web.command.create_channel", "Create channel"};
    }
    CommandDescription operator()(const application::UpdateChannelSweepCommand&) const {
        return {"web.command.update_channel_sweep", "Update channel sweep"};
    }
    CommandDescription operator()(const application::CreateMeasurementCommand&) const {
        return {"web.command.create_measurement", "Create measurement"};
    }
    CommandDescription operator()(const application::CreateWindowCommand&) const {
        return {"web.command.create_window", "Create display window"};
    }
    CommandDescription operator()(const application::CreateTraceCommand&) const {
        return {"web.command.create_trace", "Create trace"};
    }
    CommandDescription operator()(const application::UpdateTraceFormatCommand&) const {
        return {"web.command.update_trace_format", "Update trace format"};
    }
    CommandDescription operator()(
        const application::SetTraceMeasurementTypeCommand&) const {
        return {"web.command.set_trace_measurement_type",
                "Set trace measurement type"};
    }
    CommandDescription operator()(
        const application::UpdateTraceScalePerDivisionCommand&) const {
        return {"web.command.update_trace_scale_per_division",
                "Update trace scale per division"};
    }
    CommandDescription operator()(const application::RemoveTraceCommand&) const {
        return {"web.command.remove_trace", "Remove trace"};
    }
    CommandDescription operator()(const application::StartSingleSweepCommand&) const {
        return {"web.command.start_single_sweep", "Start single sweep"};
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
        const auto description = std::visit(DescribeCommand{}, command.payload);
        const auto outcome = commandOutcomeInfo(result.outcome);
        const auto written = logger->write({
            .level = succeeded ? observability::LogLevel::Info
                               : observability::LogLevel::Warning,
            .name = description.event,
            .message = std::string{description.action} +
                (succeeded ? " succeeded" : " rejected"),
            .commandId = command.commandId.value(),
            .sessionId = command.sessionId.value(),
            .instrumentId = command.instrumentId.value(),
            .stateRevision = result.stateRevision,
            .status = succeeded ? "succeeded" : "rejected",
            .errorCode = outcome.errorCode == nullptr
                ? std::optional<std::string>{}
                : std::string{outcome.errorCode},
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
