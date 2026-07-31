#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <vna/display_model/display_workspace.hpp>
#include <vna/domain/instrument.hpp>

namespace vna::application {

template <typename Tag>
class TextId {
public:
    explicit TextId(std::string value) : value_(std::move(value)) {
        if (value_.empty() || value_.size() > 128) {
            throw std::invalid_argument{"TextId must be 1..128 bytes"};
        }
        for (const unsigned char byte : value_) {
            if (byte <= 0x1F || byte == 0x7F) {
                throw std::invalid_argument{
                    "TextId must not contain ASCII control bytes"};
            }
        }
    }

    TextId(const TextId&) = default;
    TextId& operator=(const TextId&) = default;

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
    display_model::WindowId windowId;
    domain::MeasurementId measurementId;
    display_model::TraceFormat format;
};

struct UpdateTraceFormatCommand {
    display_model::TraceId traceId;
    display_model::TraceFormat format;
};

struct UpdateTraceScalePerDivisionCommand {
    display_model::TraceId traceId;
    double scalePerDivision;
};

struct RemoveTraceCommand {
    display_model::TraceId traceId;
};

using CommandPayload = std::variant<
    CreateChannelCommand,
    UpdateChannelSweepCommand,
    CreateMeasurementCommand,
    CreateWindowCommand,
    CreateTraceCommand,
    UpdateTraceFormatCommand,
    UpdateTraceScalePerDivisionCommand,
    RemoveTraceCommand>;

using CommandValue = std::variant<
    std::monostate,
    domain::ChannelId,
    domain::MeasurementId,
    display_model::WindowId,
    display_model::TraceId>;

enum class CommandOrigin {
    Web,
    Scpi,
};

struct CommandEnvelope {
    CommandId commandId;
    SessionId sessionId;
    InstrumentId instrumentId;
    CommandOrigin origin;
    std::optional<std::uint64_t> expectedStateRevision;
    CommandPayload payload;
};

enum class ApplicationErrorCode {
    CommandIdReuse,
    StateRevisionConflict,
    WrongInstrument,
};

enum class CommandErrorCode {
    InvalidSweepSettings,
    ChannelNotFound,
    MeasurementNotFound,
    WindowNotFound,
    TraceNotFound,
    InvalidScalePerDivision,
    ScaleNotSupportedForFormat,
    CommandIdReuse,
    StateRevisionConflict,
    WrongInstrument,
};

struct ApplicationError {
    ApplicationErrorCode code;
};

struct CommandSuccess {
    CommandValue value{};
};

using CommandError = std::variant<
    domain::DomainError,
    display_model::DisplayError,
    ApplicationError>;
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
    display_model::DisplayWorkspaceSnapshot display;
};

struct CommandBusStats {
    std::size_t idempotencyEntries{};
    std::uint64_t idempotencyEvictions{};
};

class CommandBus {
public:
    explicit CommandBus(
        InstrumentId instrumentId,
        std::size_t idempotencyCapacity = 1024);
    ~CommandBus();

    [[nodiscard]] CommandResult dispatch(const CommandEnvelope& command);
    [[nodiscard]] StateSnapshot snapshot() const;
    [[nodiscard]] CommandBusStats stats() const;

private:
    class IdempotencyStore;

    [[nodiscard]] CommandResult execute(const CreateChannelCommand& command);
    [[nodiscard]] CommandResult execute(
        const UpdateChannelSweepCommand& command);
    [[nodiscard]] CommandResult execute(
        const CreateMeasurementCommand& command);
    [[nodiscard]] CommandResult execute(const CreateWindowCommand& command);
    [[nodiscard]] CommandResult execute(const CreateTraceCommand& command);
    [[nodiscard]] CommandResult execute(
        const UpdateTraceFormatCommand& command);
    [[nodiscard]] CommandResult execute(
        const UpdateTraceScalePerDivisionCommand& command);
    [[nodiscard]] CommandResult execute(const RemoveTraceCommand& command);
    [[nodiscard]] CommandResult succeeded(CommandValue value);
    [[nodiscard]] CommandResult domainError(domain::DomainError error) const;
    [[nodiscard]] CommandResult displayError(
        display_model::DisplayError error) const;
    [[nodiscard]] CommandResult applicationError(
        ApplicationErrorCode code) const;

    InstrumentId instrumentId_;
    mutable std::mutex mutex_;
    domain::Instrument instrument_;
    display_model::DisplayWorkspace displayWorkspace_;
    std::uint64_t stateRevision_{0};
    std::unique_ptr<IdempotencyStore> idempotency_;
};

}  // namespace vna::application
