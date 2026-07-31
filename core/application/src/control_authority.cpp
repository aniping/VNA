#include "control_authority_internal.hpp"

#include <stdexcept>
#include <utility>

namespace vna::application {
namespace {

ControlResult controlResult(
    std::uint64_t revision,
    const std::optional<ApplicationErrorCode>& error,
    ControlSnapshot snapshot) {
    if (error.has_value()) {
        return {revision, ApplicationError{*error}};
    }
    return {revision, snapshot};
}

}  // namespace

std::optional<ApplicationErrorCode> CommandBus::ControlAuthority::tryAttach(
    const SessionId& sessionId,
    ScpiSessionRevoker& revoker) {
    if (!std::holds_alternative<LocalNoLease>(state_)) {
        return ApplicationErrorCode::ResourceBusy;
    }
    state_.emplace<LocalAttached>(sessionId, std::move(revoker));
    return std::nullopt;
}

CommandBus::ControlAuthority::Transition
CommandBus::ControlAuthority::activate(const SessionId& sessionId) {
    if (auto* local = std::get_if<LocalAttached>(&state_)) {
        if (local->owner != sessionId) {
            return {.error = ApplicationErrorCode::ControlDenied};
        }
        auto attached = std::move(*local);
        state_.emplace<RemoteAttached>(
            std::move(attached.owner), std::move(attached.revoker));
        return {.revisionChanged = true};
    }
    const auto* remote = std::get_if<RemoteAttached>(&state_);
    if (remote != nullptr && remote->owner == sessionId) {
        return {};
    }
    return {.error = ApplicationErrorCode::ControlDenied};
}

CommandBus::ControlAuthority::Transition
CommandBus::ControlAuthority::detach(const SessionId& sessionId) {
    if (std::holds_alternative<LocalNoLease>(state_)) {
        return {};
    }
    if (const auto* revoking = std::get_if<LocalRevoking>(&state_)) {
        if (revoking->owner != sessionId) {
            return {.error = ApplicationErrorCode::ControlDenied};
        }
        state_.emplace<LocalNoLease>();
        return {};
    }
    auto* remote = std::get_if<RemoteAttached>(&state_);
    auto* local = std::get_if<LocalAttached>(&state_);
    const SessionId& owner = remote != nullptr ? remote->owner : local->owner;
    if (sessionId != owner) {
        return {.error = ApplicationErrorCode::ControlDenied};
    }
    const bool wasRemote = remote != nullptr;
    auto revoker = wasRemote
        ? std::move(remote->revoker)
        : std::move(local->revoker);
    state_.emplace<LocalNoLease>();
    return {
        .revisionChanged = wasRemote,
        .deferredRevoker = std::move(revoker),
    };
}

CommandBus::ControlAuthority::Transition
CommandBus::ControlAuthority::takeLocal() {
    if (std::holds_alternative<LocalNoLease>(state_) ||
        std::holds_alternative<LocalRevoking>(state_)) {
        return {};
    }
    if (auto* local = std::get_if<LocalAttached>(&state_)) {
        auto attached = std::move(*local);
        state_.emplace<LocalRevoking>(std::move(attached.owner));
        return {
            .invokeRevoker = true,
            .deferredRevoker = std::move(attached.revoker),
        };
    }
    auto attached = std::move(std::get<RemoteAttached>(state_));
    state_.emplace<LocalRevoking>(std::move(attached.owner));
    return {
        .revisionChanged = true,
        .invokeRevoker = true,
        .deferredRevoker = std::move(attached.revoker),
    };
}

bool CommandBus::ControlAuthority::authorizes(
    CommandOrigin origin,
    const SessionId& sessionId) const noexcept {
    const auto* remote = std::get_if<RemoteAttached>(&state_);
    if (remote != nullptr) {
        return origin == CommandOrigin::Scpi && remote->owner == sessionId;
    }
    return origin == CommandOrigin::Web;
}

ControlSnapshot CommandBus::ControlAuthority::snapshot() const noexcept {
    return {
        .mode = std::holds_alternative<RemoteAttached>(state_)
            ? ControlMode::Remote
            : ControlMode::Local,
    };
}

ControlResult CommandBus::tryAttachScpiSession(
    const SessionId& sessionId,
    ScpiSessionRevoker revoker) {
    if (!revoker) {
        throw std::invalid_argument{"SCPI session revoker must not be empty"};
    }
    const std::scoped_lock lock{mutex_};
    const auto error = controlAuthority_->tryAttach(sessionId, revoker);
    return controlResult(
        stateRevision_, error, controlAuthority_->snapshot());
}

ControlResult CommandBus::activateScpiControl(const SessionId& sessionId) {
    const std::scoped_lock lock{mutex_};
    const auto transition = controlAuthority_->activate(sessionId);
    if (transition.revisionChanged) {
        ++stateRevision_;
    }
    return controlResult(
        stateRevision_, transition.error, controlAuthority_->snapshot());
}

ControlResult CommandBus::detachScpiSession(const SessionId& sessionId) {
    ScpiSessionRevoker deferredRevoker;
    ControlResult result{0, ControlSnapshot{}};
    {
        const std::scoped_lock lock{mutex_};
        auto transition = controlAuthority_->detach(sessionId);
        if (transition.revisionChanged) {
            ++stateRevision_;
        }
        deferredRevoker = std::move(transition.deferredRevoker);
        result = controlResult(
            stateRevision_, transition.error, controlAuthority_->snapshot());
    }
    return result;
}

ControlResult CommandBus::takeLocalControl() {
    ScpiSessionRevoker revoker;
    bool invokeRevoker = false;
    ControlResult result{0, ControlSnapshot{}};
    {
        const std::scoped_lock lock{mutex_};
        auto transition = controlAuthority_->takeLocal();
        if (transition.revisionChanged) {
            ++stateRevision_;
        }
        invokeRevoker = transition.invokeRevoker;
        revoker = std::move(transition.deferredRevoker);
        result = controlResult(
            stateRevision_, transition.error, controlAuthority_->snapshot());
    }
    if (invokeRevoker) {
        try {
            revoker();
        } catch (...) {
        }
    }
    return result;
}

}  // namespace vna::application
