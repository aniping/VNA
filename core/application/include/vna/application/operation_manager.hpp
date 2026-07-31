#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <variant>

#include <vna/application/command_bus.hpp>

namespace vna::application {

class OperationId {
public:
    explicit constexpr OperationId(std::uint64_t value) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    friend constexpr bool operator==(OperationId, OperationId) = default;

private:
    std::uint64_t value_;
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
    [[nodiscard]] OperationSnapshot create(OperationSubmission submission);
    [[nodiscard]] OperationResult markRunning(OperationId operationId);
    [[nodiscard]] OperationResult requestCancel(OperationId operationId);
    [[nodiscard]] OperationResult complete(
        OperationId operationId,
        OperationTerminalOutcome outcome);
    [[nodiscard]] OperationResult snapshot(OperationId operationId) const;

private:
    mutable std::mutex mutex_;
    std::uint64_t nextOperationId_{1};
    std::unordered_map<std::uint64_t, OperationSnapshot> operations_;
};

}  // namespace vna::application
