#pragma once

#include <optional>
#include <utility>
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

        Transition() = default;
        explicit Transition(ApplicationErrorCode value) : error{value} {}
        Transition(
            bool changed,
            bool invoke,
            ScpiSessionRevoker revoker = {})
            : revisionChanged{changed},
              invokeRevoker{invoke},
              deferredRevoker{std::move(revoker)} {}
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

        LocalAttached(SessionId sessionId, ScpiSessionRevoker revoke)
            : owner{std::move(sessionId)}, revoker{std::move(revoke)} {}
    };

    struct RemoteAttached {
        SessionId owner;
        ScpiSessionRevoker revoker;

        RemoteAttached(SessionId sessionId, ScpiSessionRevoker revoke)
            : owner{std::move(sessionId)}, revoker{std::move(revoke)} {}
    };

    struct LocalRevoking {
        SessionId owner;

        explicit LocalRevoking(SessionId sessionId)
            : owner{std::move(sessionId)} {}
    };

    std::variant<
        LocalNoLease,
        LocalAttached,
        RemoteAttached,
        LocalRevoking> state_{LocalNoLease{}};
};

}  // namespace vna::application
