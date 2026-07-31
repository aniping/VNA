#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <vna/domain/instrument.hpp>

namespace vna::application {

template <typename Tag>
class TextId {
public:
    explicit TextId(std::string value) : value_(std::move(value)) {}

    [[nodiscard]] const std::string& value() const noexcept {
        return value_;
    }

    friend bool operator==(const TextId&, const TextId&) = default;

private:
    std::string value_;
};

using CommandId = TextId<struct CommandIdTag>;
using SessionId = TextId<struct SessionIdTag>;
using InstrumentId = TextId<struct InstrumentIdTag>;

enum class CommandPriority {
    Normal,
};

struct CreateChannelCommand {
    domain::SweepSettings sweep;
};

struct UpdateChannelSweepCommand {
    domain::ChannelId channelId;
    domain::SweepSettings sweep;
};

struct CreateMeasurementCommand {
    domain::ChannelId channelId;
    domain::MeasurementType type;
};

struct CreateWindowCommand {};

struct CreateTraceCommand {
    domain::WindowId windowId;
    domain::MeasurementId measurementId;
    domain::TraceFormat format;
};

struct UpdateTraceFormatCommand {
    domain::TraceId traceId;
    domain::TraceFormat format;
};

struct RemoveTraceCommand {
    domain::TraceId traceId;
};

using CommandPayload = std::variant<
    CreateChannelCommand,
    UpdateChannelSweepCommand,
    CreateMeasurementCommand,
    CreateWindowCommand,
    CreateTraceCommand,
    UpdateTraceFormatCommand,
    RemoveTraceCommand>;

using CommandValue = std::variant<
    std::monostate,
    domain::ChannelId,
    domain::MeasurementId,
    domain::WindowId,
    domain::TraceId>;

struct CommandEnvelope {
    CommandId commandId;
    SessionId sessionId;
    InstrumentId instrumentId;
    std::optional<std::uint64_t> expectedStateRevision;
    std::chrono::milliseconds timeout;
    CommandPriority priority;
    CommandPayload payload;
};

enum class CommandErrorCode {
    InvalidSweepSettings,
    ChannelNotFound,
    MeasurementNotFound,
    WindowNotFound,
    TraceNotFound,
    StateRevisionConflict,
    WrongInstrument,
};

struct ApplicationError {
    CommandErrorCode code;
};

struct CommandSuccess {
    CommandValue value{};
};

using CommandError = std::variant<domain::DomainError, ApplicationError>;
using CommandOutcome = std::variant<CommandSuccess, CommandError>;

[[nodiscard]] CommandErrorCode commandErrorCode(
    const CommandError& error) noexcept;

struct CommandResult {
    std::uint64_t stateRevision;
    CommandOutcome outcome;
};

struct StateSnapshot {
    std::uint64_t stateRevision;
    domain::InstrumentSnapshot instrument;
};

class CommandBus {
public:
    explicit CommandBus(InstrumentId instrumentId);

    [[nodiscard]] CommandResult dispatch(const CommandEnvelope& command);
    [[nodiscard]] StateSnapshot snapshot() const;

private:
    [[nodiscard]] CommandResult execute(const CreateChannelCommand& command);
    [[nodiscard]] CommandResult execute(
        const UpdateChannelSweepCommand& command);
    [[nodiscard]] CommandResult execute(
        const CreateMeasurementCommand& command);
    [[nodiscard]] CommandResult execute(const CreateWindowCommand& command);
    [[nodiscard]] CommandResult execute(const CreateTraceCommand& command);
    [[nodiscard]] CommandResult execute(
        const UpdateTraceFormatCommand& command);
    [[nodiscard]] CommandResult execute(const RemoveTraceCommand& command);
    [[nodiscard]] CommandResult succeeded(CommandValue value);
    [[nodiscard]] CommandResult domainError(domain::DomainError error) const;
    [[nodiscard]] CommandResult applicationError(
        CommandErrorCode code) const;

    InstrumentId instrumentId_;
    mutable std::mutex mutex_;
    domain::Instrument instrument_;
    std::uint64_t stateRevision_{0};
};

}  // namespace vna::application
