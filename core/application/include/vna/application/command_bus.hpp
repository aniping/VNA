#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <variant>

#include <vna/application/command_contract.hpp>
#include <vna/display_model/display_workspace.hpp>
#include <vna/domain/instrument.hpp>

namespace vna::application {

enum class ApplicationErrorCode {
    CommandIdReuse,
    ControlDenied,
    ResourceBusy,
    StateRevisionConflict,
    TraceConfigurationRejected,
    UnsupportedSweepConfiguration,
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
    ControlDenied,
    ResourceBusy,
    StateRevisionConflict,
    TraceConfigurationRejected,
    UnsupportedSweepConfiguration,
    WrongInstrument,
};

struct ApplicationError {
    ApplicationErrorCode code;
};

enum class ControlMode {
    Local,
    Remote,
};

struct ControlSnapshot {
    ControlMode mode{ControlMode::Local};
};

using ControlOutcome = std::variant<ControlSnapshot, ApplicationError>;

struct ControlResult {
    std::uint64_t stateRevision;
    ControlOutcome outcome;
};

using ScpiSessionRevoker = std::function<void()>;

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
[[nodiscard]] CommandErrorCode commandErrorCode(
    const ApplicationError& error) noexcept;

struct CommandResult {
    std::uint64_t stateRevision;
    CommandOutcome outcome;
};

struct StateSnapshot {
    std::uint64_t stateRevision;
    ControlSnapshot control;
    domain::InstrumentSnapshot instrument;
    display_model::DisplayWorkspaceSnapshot display;
};

struct CommandBusStats {
    std::size_t idempotencyEntries{};
    std::uint64_t idempotencyEvictions{};
};

class SingleSweepCommandHandler;
class TracePublicationCatalog;
struct CommandBusInitialState;

class CommandBus {
public:
    // This compatibility constructor explicitly starts empty; production may
    // opt into a fully assembled state through the overload below.
    explicit CommandBus(
        InstrumentId instrumentId,
        SingleSweepCommandHandler& singleSweepHandler,
        TracePublicationCatalog& tracePublicationCatalog,
        std::size_t idempotencyCapacity = 1024);
    explicit CommandBus(
        InstrumentId instrumentId,
        SingleSweepCommandHandler& singleSweepHandler,
        TracePublicationCatalog& tracePublicationCatalog,
        CommandBusInitialState initialState,
        std::size_t idempotencyCapacity = 1024);
    ~CommandBus();

    // Attached/revoking sessions use SessionIds never reused during this bus life.
    // The bus outlives them; detach follows isolation and the final dispatch, and
    // precedes bus destruction. Public calls must not race destruction.
    // Revokers are lifetime-safe, quick/final, reentrant, and never await takeover.
    [[nodiscard]] CommandResult dispatch(const CommandEnvelope& command);
    [[nodiscard]] ControlResult tryAttachScpiSession(
        const SessionId& sessionId,
        ScpiSessionRevoker revoker);
    [[nodiscard]] ControlResult activateScpiControl(
        const SessionId& sessionId);
    [[nodiscard]] ControlResult detachScpiSession(
        const SessionId& sessionId);
    [[nodiscard]] ControlResult takeLocalControl();
    [[nodiscard]] StateSnapshot snapshot() const;
    [[nodiscard]] CommandBusStats stats() const;

private:
    class ControlAuthority;
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
    [[nodiscard]] CommandResult execute(
        const StartSingleSweepCommand& command,
        const CommandEnvelope& envelope);
    [[nodiscard]] CommandResult commitTraceConfiguration(
        domain::Instrument candidateInstrument,
        display_model::DisplayWorkspace candidateDisplay,
        CommandValue value);
    [[nodiscard]] CommandResult succeeded(CommandValue value);
    [[nodiscard]] CommandResult succeededWithoutRevision(
        CommandValue value) const;
    [[nodiscard]] CommandResult domainError(domain::DomainError error) const;
    [[nodiscard]] CommandResult displayError(
        display_model::DisplayError error) const;
    [[nodiscard]] CommandResult applicationError(
        ApplicationErrorCode code) const;

    InstrumentId instrumentId_;
    // Non-owning; the composition root keeps the handler alive past this bus.
    SingleSweepCommandHandler& singleSweepHandler_;
    // The repository-backed catalog is the single publication identity owner.
    // Composition keeps it alive past both CommandBus and every publisher.
    TracePublicationCatalog& tracePublicationCatalog_;
    mutable std::mutex mutex_;
    domain::Instrument instrument_;
    display_model::DisplayWorkspace displayWorkspace_;
    std::uint64_t stateRevision_{0};
    std::unique_ptr<IdempotencyStore> idempotency_;
    std::unique_ptr<ControlAuthority> controlAuthority_;
};

}  // namespace vna::application
