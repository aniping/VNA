#pragma once

#include "vna/core/result.hpp"
#include "vna/core/strong_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vna::runtime {

using WorkId = core::StrongId<struct WorkIdTag>;

enum class RuntimeErrc {
    ResourceExhausted,
    InvalidPermit
};

struct RuntimeError final {
    RuntimeErrc code{RuntimeErrc::InvalidPermit};
};

enum class RuntimeTerminalKind {
    Completed,
    Failed
};

struct RuntimeTerminal final {
    RuntimeTerminalKind kind{RuntimeTerminalKind::Failed};
};

class RuntimeWork {
public:
    virtual ~RuntimeWork() = default;
    virtual RuntimeTerminal execute() noexcept = 0;
};

class RuntimeCompletionSink {
public:
    virtual ~RuntimeCompletionSink() = default;
    virtual void on_runtime_terminal(
        WorkId work,
        RuntimeTerminal terminal) noexcept = 0;
};

class RuntimeCompletionRegistration final {
public:
    explicit RuntimeCompletionRegistration(RuntimeCompletionSink& sink) noexcept
        : sink_(&sink) {}
    RuntimeCompletionRegistration(RuntimeCompletionRegistration&& other) noexcept;
    RuntimeCompletionRegistration& operator=(
        RuntimeCompletionRegistration&& other) noexcept;
    RuntimeCompletionRegistration(const RuntimeCompletionRegistration&) = delete;
    RuntimeCompletionRegistration& operator=(const RuntimeCompletionRegistration&) = delete;

    bool valid() const noexcept { return sink_ != nullptr; }

private:
    friend class OperationRuntime;
    RuntimeCompletionSink* take_sink() noexcept;

    RuntimeCompletionSink* sink_{nullptr};
};

class OperationRuntime;

class ReservedWorkDispatch final {
public:
    ReservedWorkDispatch(ReservedWorkDispatch&& other) noexcept;
    ReservedWorkDispatch& operator=(ReservedWorkDispatch&& other) noexcept;
    ReservedWorkDispatch(const ReservedWorkDispatch&) = delete;
    ReservedWorkDispatch& operator=(const ReservedWorkDispatch&) = delete;
    ~ReservedWorkDispatch();

    bool valid() const noexcept { return owner_ != nullptr; }
    WorkId work_id() const noexcept { return work_id_; }

private:
    friend class OperationRuntime;
    ReservedWorkDispatch(
        OperationRuntime& owner,
        std::size_t slot,
        std::uint64_t generation,
        WorkId work_id) noexcept;
    void release() noexcept;
    void invalidate() noexcept;

    OperationRuntime* owner_{nullptr};
    std::size_t slot_{0U};
    std::uint64_t generation_{0U};
    WorkId work_id_{};
};

struct DispatchReceipt final {
    WorkId work{};
};

struct RuntimeSnapshot final {
    std::size_t reserved{0U};
    std::size_t queued{0U};
    std::size_t running{0U};
    std::uint64_t completed{0U};
};

class OperationRuntime final {
public:
    static constexpr std::size_t kMaximumSlots = 16U;

    explicit OperationRuntime(std::size_t capacity) noexcept;

    core::Result<ReservedWorkDispatch, RuntimeError> reserve_work(
        WorkId work) noexcept;
    core::Result<DispatchReceipt, RuntimeError> dispatch(
        ReservedWorkDispatch&& reservation,
        RuntimeWork& work,
        RuntimeCompletionRegistration&& completion) noexcept;
    bool run_one() noexcept;
    RuntimeSnapshot inspect() const noexcept;

private:
    friend class ReservedWorkDispatch;

    enum class SlotState {
        Empty,
        Reserved,
        Queued,
        Running
    };

    struct Slot final {
        SlotState state{SlotState::Empty};
        std::uint64_t generation{0U};
        WorkId work_id{};
        RuntimeWork* work{nullptr};
        RuntimeCompletionSink* completion{nullptr};
    };

    void release_reservation(
        std::size_t slot,
        std::uint64_t generation) noexcept;

    std::array<Slot, kMaximumSlots> slots_{};
    std::size_t capacity_{0U};
    std::uint64_t next_generation_{1U};
    std::uint64_t completed_{0U};
};

}  // namespace vna::runtime
