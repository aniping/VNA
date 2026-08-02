#include "command_business_log.hpp"

#include "command_outcome_info.hpp"

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <spdlog/spdlog.h>

namespace vna::web_api::detail {
namespace {

const char* measurementName(domain::MeasurementType type) noexcept {
    switch (type) {
        case domain::MeasurementType::S11: return "S11";
        case domain::MeasurementType::S21: return "S21";
        case domain::MeasurementType::S12: return "S12";
        case domain::MeasurementType::S22: return "S22";
    }
    return "未知测量";
}

const char* formatName(display_model::TraceFormat format) noexcept {
    switch (format) {
        case display_model::TraceFormat::LogMagnitude: return "对数幅度";
        case display_model::TraceFormat::Phase: return "相位";
        case display_model::TraceFormat::Smith: return "Smith";
    }
    return "未知格式";
}

std::string describe(const application::CreateChannelCommand&) {
    return "创建通道";
}
std::string describe(const application::UpdateChannelSweepCommand& command) {
    return "更新通道#" + std::to_string(command.channelId.value()) +
           "扫频设置";
}
std::string describe(const application::CreateMeasurementCommand& command) {
    return "为通道#" + std::to_string(command.channelId.value()) + "创建" +
           measurementName(command.type) + "测量";
}
std::string describe(const application::CreateWindowCommand&) {
    return "创建显示窗口";
}
std::string describe(const application::CreateTraceCommand& command) {
    return "在窗口#" + std::to_string(command.windowId.value()) + "为测量#" +
           std::to_string(command.measurementId.value()) + "创建" +
           formatName(command.format) + "迹线";
}
std::string describe(const application::UpdateTraceFormatCommand& command) {
    return "将迹线#" + std::to_string(command.traceId.value()) +
           "格式设置为" + formatName(command.format);
}
std::string describe(
    const application::SetTraceMeasurementTypeCommand& command) {
    return "将迹线#" + std::to_string(command.traceId.value()) +
           "测量设置为" + measurementName(command.measurementType);
}
std::string describe(
    const application::UpdateTraceScalePerDivisionCommand& command) {
    std::ostringstream text;
    text << "将迹线#" << command.traceId.value() << "每格设置为"
         << command.scalePerDivision << " dB";
    return text.str();
}
std::string describe(const application::RemoveTraceCommand& command) {
    return "删除迹线#" + std::to_string(command.traceId.value());
}
std::string describe(const application::EnsureAllSParametersCommand& command) {
    return "为迹线#" + std::to_string(command.traceId.value()) +
           "补齐全部 S 参数";
}
std::string describe(const application::StartSingleSweepCommand& command) {
    return "启动通道#" + std::to_string(command.channelId.value()) +
           "单次扫频";
}

std::string resultIdentity(const application::CommandValue& value) {
    return std::visit(
        [](const auto& identity) -> std::string {
            using Identity = std::decay_t<decltype(identity)>;
            if constexpr (std::is_same_v<Identity, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<Identity, domain::ChannelId>) {
                return " | channel_id=" + std::to_string(identity.value());
            } else if constexpr (
                std::is_same_v<Identity, domain::MeasurementId>) {
                return " | measurement_id=" + std::to_string(identity.value());
            } else if constexpr (
                std::is_same_v<Identity, display_model::WindowId>) {
                return " | window_id=" + std::to_string(identity.value());
            } else if constexpr (
                std::is_same_v<Identity, display_model::TraceId>) {
                return " | trace_id=" + std::to_string(identity.value());
            } else {
                return " | operation_id=" + std::to_string(identity.value());
            }
        },
        value);
}

const char* category(const application::CommandPayload& payload) noexcept {
    return std::holds_alternative<application::StartSingleSweepCommand>(payload)
               ? "[单次扫频]"
               : "[配置命令]";
}

}  // namespace

void logBusinessCommand(
    const application::CommandEnvelope& command,
    const application::CommandResult& result) noexcept {
    try {
        const auto logger = spdlog::get("vna");
        if (logger == nullptr) {
            return;
        }
        const auto action = std::visit(
            [](const auto& payload) { return describe(payload); },
            command.payload);
        const auto* success =
            std::get_if<application::CommandSuccess>(&result.outcome);
        if (success != nullptr) {
            logger->info(
                "{} {}请求已成功处理 | command_id={} | session_id={} | "
                "instrument_id={} | revision={}{}",
                category(command.payload), action, command.commandId.value(),
                command.sessionId.value(), command.instrumentId.value(),
                result.stateRevision,
                resultIdentity(success->value));
            return;
        }
        const auto info = commandOutcomeInfo(result.outcome);
        logger->warn(
            "{} {}请求被拒绝 | command_id={} | session_id={} | "
            "instrument_id={} | revision={} | error_code={}",
            category(command.payload), action, command.commandId.value(),
            command.sessionId.value(), command.instrumentId.value(),
            result.stateRevision, info.errorCode);
    } catch (...) {
        // Logging is diagnostic only; HTTP must preserve the dispatched result.
    }
}

}  // namespace vna::web_api::detail
