#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <variant>

#include <vna/application/command_bus.hpp>
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
struct OperationSucceeded {};

struct OperationFailed {
    CommandError error;
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
};

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
