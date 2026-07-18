#include "runtime/function/operation/operation_runtime.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace vna::runtime {

bool BudgetHandle::try_consume(std::uint64_t units) noexcept {
    if (units > remaining_) {
        return false;
    }
    remaining_ -= units;
    return true;
}

RuntimeDrainStep RuntimeWork::resume_drain(
    ExecutionContext& context) noexcept {
    (void)context;
    return RuntimeDrainStep::cleanup_failed();
}

RuntimeWorkStep ImmediateRuntimeWorkAdapter::start(
    ExecutionContext& context) noexcept {
    (void)context;
    const auto terminal = work_->execute();
    return terminal.kind == RuntimeTerminalKind::Completed
        ? RuntimeWorkStep::completed()
        : RuntimeWorkStep::failed();
}

RuntimeWorkStep ImmediateRuntimeWorkAdapter::resume(
    ExecutionContext& context) noexcept {
    (void)context;
    // 立即完成适配器的 start() 必须返回真实终态；到达这里表示契约被破坏。
    return RuntimeWorkStep::failed();
}

RuntimeDrainStep ImmediateRuntimeWorkAdapter::resume_drain(
    ExecutionContext& context) noexcept {
    (void)context;
    // 立即完成适配器不允许进入 Drain；若被调用则以清理失败终结契约异常。
    return RuntimeDrainStep::cleanup_failed();
}

RuntimeCompletionReceiver::RuntimeCompletionReceiver(
    RuntimeCompletionReceiver&& other) noexcept
    : owner_(other.owner_), id_(other.id_) {
    other.owner_ = nullptr;
    other.id_ = 0U;
}

RuntimeCompletionRegistration::RuntimeCompletionRegistration(
    OperationRuntime& owner,
    std::size_t slot,
    std::uint64_t generation,
    WorkId work,
    std::uint64_t receiver_id) noexcept
    : owner_(&owner),
      slot_(slot),
      generation_(generation),
      work_id_(work),
      receiver_id_(receiver_id) {}

RuntimeCompletionRegistration::RuntimeCompletionRegistration(
    RuntimeCompletionRegistration&& other) noexcept
    : owner_(other.owner_),
      slot_(other.slot_),
      generation_(other.generation_),
      work_id_(other.work_id_),
      receiver_id_(other.receiver_id_) {
    other.owner_ = nullptr;
    other.slot_ = 0U;
    other.generation_ = 0U;
    other.work_id_ = WorkId{};
    other.receiver_id_ = 0U;
}

RuntimeCompletionRegistration& RuntimeCompletionRegistration::operator=(
    RuntimeCompletionRegistration&& other) noexcept {
    if (this != &other) {
        owner_ = other.owner_;
        slot_ = other.slot_;
        generation_ = other.generation_;
        work_id_ = other.work_id_;
        receiver_id_ = other.receiver_id_;
        other.owner_ = nullptr;
        other.slot_ = 0U;
        other.generation_ = 0U;
        other.work_id_ = WorkId{};
        other.receiver_id_ = 0U;
    }
    return *this;
}

bool RuntimeCompletionRegistration::consume(
    OperationRuntime& owner,
    std::size_t slot,
    std::uint64_t generation,
    WorkId work,
    std::uint64_t receiver_id) noexcept {
    if (owner_ != &owner || slot_ != slot || generation_ != generation ||
        work_id_ != work || receiver_id_ != receiver_id) {
        return false;
    }
    // 固定 mailbox 已随 slot 预留；派发只消费绑定凭证，不再申请完成容量。
    owner_ = nullptr;
    slot_ = 0U;
    generation_ = 0U;
    work_id_ = WorkId{};
    receiver_id_ = 0U;
    return true;
}

ReservedWorkDispatch::ReservedWorkDispatch(
    OperationRuntime& owner,
    std::size_t slot,
    std::uint64_t generation,
    WorkId work_id,
    RuntimeCompletionRegistration&& completion) noexcept
    : owner_(&owner),
      slot_(slot),
      generation_(generation),
      work_id_(work_id),
      completion_(std::move(completion)) {}

ReservedWorkDispatch::ReservedWorkDispatch(ReservedWorkDispatch&& other) noexcept
    : owner_(other.owner_),
      slot_(other.slot_),
      generation_(other.generation_),
      work_id_(other.work_id_),
      completion_(std::move(other.completion_)) {
    other.invalidate();
}

ReservedWorkDispatch& ReservedWorkDispatch::operator=(
    ReservedWorkDispatch&& other) noexcept {
    if (this != &other) {
        release();
        owner_ = other.owner_;
        slot_ = other.slot_;
        generation_ = other.generation_;
        work_id_ = other.work_id_;
        completion_ = std::move(other.completion_);
        other.invalidate();
    }
    return *this;
}

ReservedWorkDispatch::~ReservedWorkDispatch() {
    // 预留和派发之间的任意提前返回都通过 RAII 自动归还固定槽位。
    release();
}

void ReservedWorkDispatch::release() noexcept {
    if (owner_ != nullptr) {
        owner_->release_reservation(slot_, generation_);
        invalidate();
    }
}

void ReservedWorkDispatch::invalidate() noexcept {
    owner_ = nullptr;
    slot_ = 0U;
    generation_ = 0U;
    work_id_ = WorkId{};
    completion_ = RuntimeCompletionRegistration{};
}

OperationRuntime::OperationRuntime(
    std::size_t capacity,
    RuntimeMonotonicClock& clock) noexcept
    : capacity_(std::min(capacity, kMaximumSlots)), clock_(&clock) {}

RuntimeCompletionReceiver OperationRuntime::register_completion_receiver() noexcept {
    return RuntimeCompletionReceiver{*this, next_receiver_id_++};
}

core::Result<ReservedWorkDispatch, RuntimeError> OperationRuntime::reserve_work(
    WorkId work,
    ExecutionLimits limits,
    const RuntimeCompletionReceiver& receiver) noexcept {
    if (!work.valid() || !receiver.valid() || receiver.owner_ != this) {
        return core::Result<ReservedWorkDispatch, RuntimeError>::failure(
            RuntimeError{RuntimeErrc::InvalidPermit});
    }
    if (limits.budget_units == 0U ||
        limits.budget_units == std::numeric_limits<std::uint64_t>::max() ||
        limits.deadline_tick == std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<ReservedWorkDispatch, RuntimeError>::failure(
            RuntimeError{RuntimeErrc::InvalidExecutionLimits});
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        if (slots_[index].state != SlotState::Empty &&
            slots_[index].work_id == work) {
            return core::Result<ReservedWorkDispatch, RuntimeError>::failure(
                RuntimeError{RuntimeErrc::DuplicateWorkId});
        }
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.state != SlotState::Empty) {
            continue;
        }

        // generation 防止已经析构的旧凭证误释放后来复用同一索引的新槽位。
        slot.state = SlotState::Reserved;
        slot.generation = next_generation_++;
        slot.work_id = work;
        slot.receiver_id = receiver.id_;
        slot.deadline.enabled_ = true;
        slot.deadline.tick_ = limits.deadline_tick;
        slot.deadline.clock_ = clock_;
        slot.budget.remaining_ = limits.budget_units;
        auto registration = RuntimeCompletionRegistration{
            *this, index, slot.generation, work, receiver.id_};
        return core::Result<ReservedWorkDispatch, RuntimeError>::success(
            ReservedWorkDispatch{
                *this,
                index,
                slot.generation,
                work,
                std::move(registration)});
    }

    return core::Result<ReservedWorkDispatch, RuntimeError>::failure(
        RuntimeError{RuntimeErrc::ResourceExhausted});
}

core::Result<DispatchReceipt, RuntimeError> OperationRuntime::dispatch(
    ReservedWorkDispatch&& reservation,
    RuntimeWork& work) noexcept {
    if (!reservation.valid() || !reservation.completion_.valid() ||
        reservation.owner_ != this || reservation.slot_ >= capacity_) {
        return core::Result<DispatchReceipt, RuntimeError>::failure(
            RuntimeError{RuntimeErrc::InvalidPermit});
    }

    auto& slot = slots_[reservation.slot_];
    if (slot.state != SlotState::Reserved ||
        slot.generation != reservation.generation_ ||
        slot.work_id != reservation.work_id_) {
        return core::Result<DispatchReceipt, RuntimeError>::failure(
            RuntimeError{RuntimeErrc::InvalidPermit});
    }

    const auto completion_consumed = reservation.completion_.consume(
        *this,
        reservation.slot_,
        reservation.generation_,
        reservation.work_id_,
        slot.receiver_id);
    if (!completion_consumed) {
        return core::Result<DispatchReceipt, RuntimeError>::failure(
            RuntimeError{RuntimeErrc::InvalidPermit});
    }

    // 这里只转移预留的注册权并入队。start() 必须由后续 run_one() 显式驱动，
    // 从而保证调用者在 dispatch() 返回前不会收到完成回调。
    slot.state = SlotState::Queued;
    slot.work = &work;
    const auto work_id = slot.work_id;
    reservation.invalidate();
    return core::Result<DispatchReceipt, RuntimeError>::success(
        DispatchReceipt{work_id});
}

bool OperationRuntime::request_stop(WorkId work) noexcept {
    if (!work.valid()) {
        return false;
    }
    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        const auto stoppable = slot.state == SlotState::Reserved ||
            slot.state == SlotState::Queued ||
            slot.state == SlotState::Running ||
            slot.state == SlotState::Draining;
        if (stoppable && slot.work_id == work) {
            slot.stop.requested_ = true;
            return true;
        }
    }
    return false;
}

bool OperationRuntime::run_one(
    const RuntimeCompletionReceiver& receiver,
    RuntimeCompletionSink& completion) noexcept {
    if (!receiver.valid() || receiver.owner_ != this) {
        return false;
    }
    if (pumping_) {
        return false;
    }
    pumping_ = true;
    struct PumpReset final {
        bool& flag;
        ~PumpReset() { flag = false; }
    } reset{pumping_};

    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.receiver_id != receiver.id_ ||
            slot.state != SlotState::WorkTerminalReady) {
            continue;
        }

        const auto work_id = slot.work_id;
        const auto terminal = slot.pending_work_terminal;
        if (terminal.kind == RuntimeTerminalKind::Draining) {
            // handoff 交付返回前不允许开始 Drain，保持两段完成的严格先后顺序。
            slot.state = SlotState::DeliveringDrainHandoff;
            completion.on_runtime_terminal(work_id, terminal);
            slot.state = SlotState::Draining;
        } else {
            slot.state = SlotState::DeliveringWorkTerminal;
            completion.on_runtime_terminal(work_id, terminal);
            slot = Slot{};
            ++completed_;
        }
        return true;
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.receiver_id != receiver.id_ ||
            slot.state != SlotState::DrainTerminalReady) {
            continue;
        }

        const auto work_id = slot.work_id;
        const auto terminal = slot.pending_drain_terminal;
        slot.state = SlotState::DeliveringDrainTerminal;
        completion.on_runtime_drain_terminal(work_id, terminal);
        slot = Slot{};
        ++completed_;
        return true;
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.receiver_id != receiver.id_ || slot.state != SlotState::Queued) {
            continue;
        }

        slot.state = SlotState::Running;
        ExecutionContext context{
            slot.stop, slot.deadline, slot.budget, progress_};
        handle_work_step(slot, slot.work->start(context));
        return true;
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.receiver_id != receiver.id_ || slot.state != SlotState::Running) {
            continue;
        }

        ExecutionContext context{
            slot.stop, slot.deadline, slot.budget, progress_};
        handle_work_step(slot, slot.work->resume(context));
        return true;
    }

    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.receiver_id != receiver.id_ || slot.state != SlotState::Draining) {
            continue;
        }

        ExecutionContext context{
            slot.stop, slot.deadline, slot.budget, progress_};
        handle_drain_step(slot, slot.work->resume_drain(context));
        return true;
    }
    return false;
}

void OperationRuntime::handle_work_step(
    Slot& slot,
    RuntimeWorkStep step) noexcept {
    if (step.kind() == RuntimeWorkStepKind::Running) {
        return;
    }

    if (step.kind() == RuntimeWorkStepKind::Draining && step.drain().valid()) {
        // 先写入预留 mailbox；handoff 只能由后续 Control Executor pump 交付。
        slot.drain = step.drain();
        slot.pending_work_terminal = RuntimeTerminal{
            RuntimeTerminalKind::Draining, slot.drain};
        slot.state = SlotState::WorkTerminalReady;
        return;
    }

    const auto terminal = step.kind() == RuntimeWorkStepKind::Completed
        ? RuntimeTerminal{RuntimeTerminalKind::Completed, DrainId{}}
        : RuntimeTerminal{RuntimeTerminalKind::Failed, DrainId{}};
    slot.pending_work_terminal = terminal;
    slot.state = SlotState::WorkTerminalReady;
}

void OperationRuntime::handle_drain_step(
    Slot& slot,
    RuntimeDrainStep step) noexcept {
    if (step.kind() == RuntimeDrainStepKind::Running) {
        return;
    }

    RuntimeDrainTerminalKind terminal_kind{RuntimeDrainTerminalKind::CleanupFailed};
    switch (step.kind()) {
        case RuntimeDrainStepKind::Running:
            return;
        case RuntimeDrainStepKind::Drained:
            terminal_kind = RuntimeDrainTerminalKind::Drained;
            break;
        case RuntimeDrainStepKind::Quarantined:
            terminal_kind = RuntimeDrainTerminalKind::Quarantined;
            break;
        case RuntimeDrainStepKind::CleanupFailed:
            terminal_kind = RuntimeDrainTerminalKind::CleanupFailed;
            break;
    }

    const auto drain_id = slot.drain;
    // 资源终态先进入同一预留 mailbox；交付前仍保持全部 Runtime 容量。
    slot.pending_drain_terminal = RuntimeDrainTerminal{drain_id, terminal_kind};
    slot.state = SlotState::DrainTerminalReady;
}

RuntimeSnapshot OperationRuntime::inspect() const noexcept {
    RuntimeSnapshot snapshot{};
    snapshot.completed = completed_;
    for (std::size_t index = 0U; index < capacity_; ++index) {
        switch (slots_[index].state) {
            case SlotState::Empty:
                break;
            case SlotState::Reserved:
                ++snapshot.reserved;
                break;
            case SlotState::Queued:
                ++snapshot.queued;
                break;
            case SlotState::Running:
                ++snapshot.running;
                break;
            case SlotState::Draining:
                ++snapshot.draining;
                break;
            case SlotState::WorkTerminalReady:
                ++snapshot.running;
                break;
            case SlotState::DrainTerminalReady:
                ++snapshot.draining;
                break;
            case SlotState::DeliveringWorkTerminal:
                ++snapshot.running;
                break;
            case SlotState::DeliveringDrainHandoff:
            case SlotState::DeliveringDrainTerminal:
                ++snapshot.draining;
                break;
        }
    }
    return snapshot;
}

void OperationRuntime::release_reservation(
    std::size_t slot,
    std::uint64_t generation) noexcept {
    if (slot < capacity_ && slots_[slot].state == SlotState::Reserved &&
        slots_[slot].generation == generation) {
        slots_[slot] = Slot{};
    }
}

}  // namespace vna::runtime
