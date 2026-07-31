#pragma once

#include <optional>
#include <variant>

#include <vna/application/command_bus.hpp>

namespace vna::application {

class CommandBus::ControlAuthority {
public:
    struct Transition {
        std::optional<ApplicationErrorCode> error{};
        bool revisionChanged{};
        bool invokeRevoker{};
        ScpiSessionRevoker deferredRevoker{};
    };

    [[nodiscard]] std::optional<ApplicationErrorCode> tryAttach(
        const SessionId& sessionId,
        ScpiSessionRevoker& revoker);
    [[nodiscard]] Transition activate(const SessionId& sessionId);
    [[nodiscard]] Transition detach(const SessionId& sessionId);
    [[nodiscard]] Transition takeLocal();
    [[nodiscard]] bool authorizes(
        CommandOrigin origin,
        const SessionId& sessionId) const noexcept;
    [[nodiscard]] ControlSnapshot snapshot() const noexcept;

private:
    struct LocalNoLease {};

    struct LocalAttached {
        SessionId owner;
        ScpiSessionRevoker revoker;
    };

    struct RemoteAttached {
        SessionId owner;
        ScpiSessionRevoker revoker;
    };

    struct LocalRevoking {
        SessionId owner;
    };

    std::variant<
        LocalNoLease,
        LocalAttached,
        RemoteAttached,
        LocalRevoking> state_{LocalNoLease{}};
};

}  // namespace vna::application
