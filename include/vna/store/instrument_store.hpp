#pragma once

#include "vna/core/result.hpp"
#include "vna/core/strong_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace vna::store {

using OperationId = core::StrongId<struct OperationIdTag>;

enum class StoreErrc {
    ResourceExhausted,
    InvalidReservation,
    InvalidOperation,
    DuplicateOperation,
    OperationNotFound
};

struct StoreError final {
    StoreErrc code{StoreErrc::InvalidReservation};
};

enum class OperationState {
    Accepted,
    Completed,
    Failed
};

struct OperationSnapshot final {
    OperationId id{};
    OperationState state{OperationState::Accepted};
    std::uint64_t revision{0U};
};

class InstrumentStore;

class LifecycleTerminalReservation final {
public:
    LifecycleTerminalReservation(LifecycleTerminalReservation&& other) noexcept;
    LifecycleTerminalReservation& operator=(
        LifecycleTerminalReservation&& other) noexcept;
    LifecycleTerminalReservation(const LifecycleTerminalReservation&) = delete;
    LifecycleTerminalReservation& operator=(const LifecycleTerminalReservation&) = delete;
    ~LifecycleTerminalReservation();

    bool valid() const noexcept { return owner_ != nullptr; }

private:
    friend class InstrumentStore;
    LifecycleTerminalReservation(
        InstrumentStore& owner,
        std::size_t slot,
        std::uint64_t generation) noexcept;
    void release() noexcept;
    void invalidate() noexcept;

    InstrumentStore* owner_{nullptr};
    std::size_t slot_{0U};
    std::uint64_t generation_{0U};
};

struct AcceptedCommitReceipt final {
    OperationId operation{};
    std::uint64_t revision{0U};
};

struct RejectedAcceptedCommit final {
    StoreError error{};
    LifecycleTerminalReservation reclaimed;
};

using AcceptedCommitResult = std::variant<
    AcceptedCommitReceipt,
    RejectedAcceptedCommit>;

enum class TerminalCommitDisposition {
    Committed,
    AlreadyTerminal
};

struct TerminalCommitReceipt final {
    OperationId operation{};
    OperationState state{OperationState::Failed};
    std::uint64_t revision{0U};
    TerminalCommitDisposition disposition{TerminalCommitDisposition::Committed};
};

struct StoreSnapshot final {
    std::size_t reserved_lifecycles{0U};
    std::size_t visible_operations{0U};
    std::uint64_t revision{0U};
};

class InstrumentStore final {
public:
    static constexpr std::size_t kMaximumOperations = 16U;

    explicit InstrumentStore(std::size_t capacity) noexcept;

    core::Result<LifecycleTerminalReservation, StoreError>
    reserve_lifecycle_terminal() noexcept;
    AcceptedCommitResult commit_accepted(
        OperationId operation,
        LifecycleTerminalReservation&& reservation) noexcept;
    core::Result<TerminalCommitReceipt, StoreError> commit_terminal(
        OperationId operation,
        OperationState terminal_state) noexcept;
    std::optional<OperationSnapshot> inspect_operation(
        OperationId operation) const noexcept;
    StoreSnapshot inspect() const noexcept;

private:
    friend class LifecycleTerminalReservation;

    enum class SlotState {
        Empty,
        Reserved,
        Visible
    };

    struct Slot final {
        SlotState slot_state{SlotState::Empty};
        std::uint64_t generation{0U};
        OperationSnapshot operation{};
    };

    void release_reservation(
        std::size_t slot,
        std::uint64_t generation) noexcept;

    std::array<Slot, kMaximumOperations> slots_{};
    std::size_t capacity_{0U};
    std::uint64_t next_generation_{1U};
    std::uint64_t revision_{0U};
};

}  // namespace vna::store
