#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#include <vna/application/command_contract.hpp>
#include <vna/application/operation_failure.hpp>
#include <vna/application/operation_id.hpp>

namespace vna::application {

namespace detail {
struct CompletionFenceState;
struct FenceCoordinator;
struct FenceSubscriptionState;
}  // namespace detail

using FenceCallback = std::function<void()>;

class CompletionFence {
public:
    CompletionFence(CompletionFence&&) noexcept = default;
    CompletionFence& operator=(CompletionFence&&) noexcept = default;
    ~CompletionFence() = default;

    CompletionFence(const CompletionFence&) = delete;
    CompletionFence& operator=(const CompletionFence&) = delete;

    [[nodiscard]] bool active() const noexcept;

private:
    explicit CompletionFence(
        std::shared_ptr<detail::CompletionFenceState> state);

    std::shared_ptr<detail::CompletionFenceState> state_;
    friend class OperationManager;
};

class FenceSubscription {
public:
    FenceSubscription(FenceSubscription&& other) noexcept;
    FenceSubscription& operator=(FenceSubscription&& other) noexcept;
    ~FenceSubscription();

    FenceSubscription(const FenceSubscription&) = delete;
    FenceSubscription& operator=(const FenceSubscription&) = delete;

    void cancel() noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    explicit FenceSubscription(
        std::shared_ptr<detail::FenceSubscriptionState> state);

    std::shared_ptr<detail::FenceSubscriptionState> state_;
    friend class OperationManager;
};

struct OperationQueued {};
struct OperationRunning {};
struct OperationCancelRequested {};
// The terminal success retains the identity committed to the display
// repository, so protocol adapters never need to reverse-search frame storage.
struct OperationSucceeded {
    frames::FrameId frameId;
};

struct OperationFailed {
    OperationFailure error;
};

struct OperationCanceled {};

using OperationState = std::variant<
    OperationQueued,
    OperationRunning,
    OperationCancelRequested,
    OperationSucceeded,
    OperationFailed,
    OperationCanceled>;

using OperationTerminalOutcome = std::variant<
    OperationSucceeded,
    OperationFailed,
    OperationCanceled>;

struct OperationSnapshot {
    OperationId id;
    CommandId commandId;
    SessionId sessionId;
    std::uint64_t submittedAtStateRevision;
    OperationState state;

    OperationSnapshot(
        OperationId operationId,
        CommandId command,
        SessionId session,
        std::uint64_t stateRevision,
        OperationState operationState) noexcept
        : id(operationId),
          commandId(std::move(command)),
          sessionId(std::move(session)),
          submittedAtStateRevision(stateRevision),
          state(std::move(operationState)) {}

    OperationSnapshot(const OperationSnapshot& other) noexcept
        : id(other.id),
          commandId(other.commandId),
          sessionId(other.sessionId),
          submittedAtStateRevision(other.submittedAtStateRevision),
          state(copyState(other.state)) {}
    OperationSnapshot& operator=(const OperationSnapshot& other) noexcept {
        if (this != &other) {
            id = other.id;
            commandId = other.commandId;
            sessionId = other.sessionId;
            submittedAtStateRevision = other.submittedAtStateRevision;
            state = copyState(other.state);
        }
        return *this;
    }
    OperationSnapshot(OperationSnapshot&&) noexcept = default;
    OperationSnapshot& operator=(OperationSnapshot&&) noexcept = default;

private:
    static OperationState copyState(const OperationState& source) noexcept {
        return std::visit(
            [](const auto& current) -> OperationState {
                return OperationState{current};
            },
            source);
    }
};

static_assert(std::is_nothrow_move_assignable_v<OperationState>);

struct OperationSubmission {
    CommandId commandId;
    SessionId sessionId;
    std::uint64_t submittedAtStateRevision;
};

enum class OperationErrorCode {
    NotFound,
    InvalidTransition,
};

struct OperationError {
    OperationErrorCode code;
};

using OperationResult = std::variant<OperationSnapshot, OperationError>;

class OperationManager {
public:
    OperationManager();
    // The owner must stop new calls and join in-flight calls before destruction.
    ~OperationManager();

    OperationManager(const OperationManager&) = delete;
    OperationManager& operator=(const OperationManager&) = delete;
    OperationManager(OperationManager&&) = delete;
    OperationManager& operator=(OperationManager&&) = delete;

    [[nodiscard]] OperationSnapshot create(OperationSubmission submission);
    [[nodiscard]] OperationResult markRunning(OperationId operationId);
    [[nodiscard]] OperationResult requestCancel(OperationId operationId);
    [[nodiscard]] OperationResult complete(
        OperationId operationId,
        OperationTerminalOutcome outcome);
    [[nodiscard]] OperationResult snapshot(OperationId operationId) const;
    [[nodiscard]] CompletionFence captureFence(const SessionId& sessionId);
    [[nodiscard]] FenceSubscription subscribe(
        CompletionFence fence,
        FenceCallback callback);

private:
    mutable std::mutex mutex_;
    std::uint64_t nextOperationId_{1};
    std::unordered_map<std::uint64_t, OperationSnapshot> operations_;
    std::shared_ptr<detail::FenceCoordinator> fenceCoordinator_;
};

}  // namespace vna::application
