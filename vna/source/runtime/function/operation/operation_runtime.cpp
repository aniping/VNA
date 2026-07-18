#include "runtime/function/operation/operation_runtime.h"

#include <algorithm>
#include <utility>

namespace vna::runtime {

RuntimeCompletionRegistration::RuntimeCompletionRegistration(
    RuntimeCompletionRegistration&& other) noexcept
    : sink_(other.sink_) {
    other.sink_ = nullptr;
}

RuntimeCompletionRegistration& RuntimeCompletionRegistration::operator=(
    RuntimeCompletionRegistration&& other) noexcept {
    if (this != &other) {
        sink_ = other.sink_;
        other.sink_ = nullptr;
    }
    return *this;
}

RuntimeCompletionSink* RuntimeCompletionRegistration::take_sink() noexcept {
    // 派发成功后由运行时持有回调注册权；源注册立即失效，避免重复派发。
    auto* sink = sink_;
    sink_ = nullptr;
    return sink;
}

ReservedWorkDispatch::ReservedWorkDispatch(
    OperationRuntime& owner,
    std::size_t slot,
    std::uint64_t generation,
    WorkId work_id) noexcept
    : owner_(&owner), slot_(slot), generation_(generation), work_id_(work_id) {}

ReservedWorkDispatch::ReservedWorkDispatch(ReservedWorkDispatch&& other) noexcept
    : owner_(other.owner_),
      slot_(other.slot_),
      generation_(other.generation_),
      work_id_(other.work_id_) {
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
}

OperationRuntime::OperationRuntime(std::size_t capacity) noexcept
    : capacity_(std::min(capacity, kMaximumSlots)) {}

core::Result<ReservedWorkDispatch, RuntimeError> OperationRuntime::reserve_work(
    WorkId work) noexcept {
    if (!work.valid()) {
        return core::Result<ReservedWorkDispatch, RuntimeError>::failure(
            RuntimeError{RuntimeErrc::InvalidPermit});
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
        return core::Result<ReservedWorkDispatch, RuntimeError>::success(
            ReservedWorkDispatch{*this, index, slot.generation, work});
    }

    return core::Result<ReservedWorkDispatch, RuntimeError>::failure(
        RuntimeError{RuntimeErrc::ResourceExhausted});
}

core::Result<DispatchReceipt, RuntimeError> OperationRuntime::dispatch(
    ReservedWorkDispatch&& reservation,
    RuntimeWork& work,
    RuntimeCompletionRegistration&& completion) noexcept {
    if (!reservation.valid() || !completion.valid() ||
        reservation.owner_ != this || reservation.slot_ >= capacity_) {
        return core::Result<DispatchReceipt, RuntimeError>::failure(
            RuntimeError{RuntimeErrc::InvalidPermit});
    }

    auto& slot = slots_[reservation.slot_];
    if (slot.state != SlotState::Reserved ||
        slot.generation != reservation.generation_) {
        return core::Result<DispatchReceipt, RuntimeError>::failure(
            RuntimeError{RuntimeErrc::InvalidPermit});
    }

    // 这里只转移注册权并入队。execute() 必须由后续 run_one() 显式驱动，
    // 从而保证调用者在 dispatch() 返回前不会收到完成回调。
    slot.state = SlotState::Queued;
    slot.work = &work;
    slot.completion = completion.take_sink();
    const auto work_id = slot.work_id;
    reservation.invalidate();
    return core::Result<DispatchReceipt, RuntimeError>::success(
        DispatchReceipt{work_id});
}

bool OperationRuntime::run_one() noexcept {
    for (std::size_t index = 0U; index < capacity_; ++index) {
        auto& slot = slots_[index];
        if (slot.state != SlotState::Queued) {
            continue;
        }

        slot.state = SlotState::Running;
        const auto work_id = slot.work_id;
        const auto terminal = slot.work->execute();
        // 回调期间保留 Running 状态，使重入查询看到真实状态；回调返回后才释放槽位。
        slot.completion->on_runtime_terminal(work_id, terminal);
        slot = Slot{};
        ++completed_;
        return true;
    }
    return false;
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
