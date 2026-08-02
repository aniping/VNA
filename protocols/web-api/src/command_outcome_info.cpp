#include "command_outcome_info.hpp"

#include <variant>

namespace vna::web_api::detail {
namespace {

CommandOutcomeInfo errorOutcomeInfo(
    application::CommandErrorCode code) noexcept {
    using application::CommandErrorCode;
    switch (code) {
    case CommandErrorCode::InvalidSweepSettings:
        return {"validationError", 422, "invalid-sweep-settings"};
    case CommandErrorCode::ChannelNotFound:
        return {"validationError", 422, "channel-not-found"};
    case CommandErrorCode::MeasurementNotFound:
        return {"validationError", 422, "measurement-not-found"};
    case CommandErrorCode::WindowNotFound:
        return {"validationError", 422, "window-not-found"};
    case CommandErrorCode::TraceNotFound:
        return {"validationError", 422, "trace-not-found"};
    case CommandErrorCode::InvalidScalePerDivision:
        return {"validationError", 422, "invalid-scale-per-division"};
    case CommandErrorCode::ScaleNotSupportedForFormat:
        return {"validationError", 422, "scale-not-supported-for-format"};
    case CommandErrorCode::CommandIdReuse:
        return {"conflict", 409, "command-id-reuse"};
    case CommandErrorCode::ControlDenied:
        return {"conflict", 409, "control-denied"};
    case CommandErrorCode::ResourceBusy:
        return {"conflict", 409, "resource-busy"};
    case CommandErrorCode::StateRevisionConflict:
        return {"conflict", 409, "state-revision-conflict"};
    case CommandErrorCode::TraceConfigurationRejected:
        return {"validationError", 422, "trace-configuration-rejected"};
    case CommandErrorCode::UnsupportedSweepConfiguration:
        return {"validationError", 422, "unsupported-sweep-configuration"};
    case CommandErrorCode::WrongInstrument:
        return {"wrongInstrument", 404, "wrong-instrument"};
    }
    return {"unknown", 500, "unknown"};
}

}  // namespace

CommandOutcomeInfo commandOutcomeInfo(
    const application::CommandOutcome& outcome) noexcept {
    if (std::holds_alternative<application::CommandSuccess>(outcome)) {
        return {"succeeded", 200, nullptr};
    }
    return errorOutcomeInfo(application::commandErrorCode(
        std::get<application::CommandError>(outcome)));
}

}  // namespace vna::web_api::detail
