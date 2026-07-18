#include "test_support.h"

#include "runtime/function/operation/operation_runtime.h"

#include <type_traits>
#include <utility>

namespace {

static_assert(!std::is_copy_constructible_v<vna::runtime::ReservedWorkDispatch>);
static_assert(std::is_nothrow_move_constructible_v<vna::runtime::ReservedWorkDispatch>);
static_assert(
    !std::is_copy_constructible_v<vna::runtime::RuntimeCompletionReceiver>);
static_assert(
    std::is_nothrow_move_constructible_v<
        vna::runtime::RuntimeCompletionReceiver>);

constexpr vna::runtime::ExecutionLimits kExecutionLimits{1000U, 64U};

class ManualRuntimeClock final : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return now_; }

    void advance(std::uint64_t ticks) noexcept { now_ += ticks; }

private:
    std::uint64_t now_{0U};
};

class CompletingWork final : public vna::runtime::ImmediateRuntimeWork {
public:
    vna::runtime::RuntimeTerminal execute() noexcept override {
        ++executions;
        return vna::runtime::RuntimeTerminal{
            vna::runtime::RuntimeTerminalKind::Completed};
    }

    std::uint32_t executions{0U};
};

class DeferredWork final : public vna::runtime::RuntimeWork {
public:
    vna::runtime::RuntimeWorkStep start(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        ++starts;
        return vna::runtime::RuntimeWorkStep::running();
    }

    vna::runtime::RuntimeWorkStep resume(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        ++resumes;
        return complete
            ? vna::runtime::RuntimeWorkStep::completed()
            : vna::runtime::RuntimeWorkStep::running();
    }

    bool complete{false};
    std::uint32_t starts{0U};
    std::uint32_t resumes{0U};
};

class DrainingWork final : public vna::runtime::RuntimeWork {
public:
    vna::runtime::RuntimeWorkStep start(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        return vna::runtime::RuntimeWorkStep::running();
    }

    vna::runtime::RuntimeWorkStep resume(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        return vna::runtime::RuntimeWorkStep::draining(
            vna::runtime::DrainId{91U});
    }

    vna::runtime::RuntimeDrainStep resume_drain(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        ++drain_resumes;
        return drain_complete
            ? vna::runtime::RuntimeDrainStep::drained()
            : vna::runtime::RuntimeDrainStep::running();
    }

    bool drain_complete{false};
    std::uint32_t drain_resumes{0U};
};

class ContextObservingWork final : public vna::runtime::RuntimeWork {
public:
    vna::runtime::RuntimeWorkStep start(
        vna::runtime::ExecutionContext& context) noexcept override {
        stop_requested_at_start = context.stop().stop_requested();
        deadline_enabled = context.deadline().enabled();
        deadline_tick = context.deadline().tick();
        deadline_expired_at_start = context.deadline().expired();
        budget_before = context.budget().remaining();
        budget_consumed = context.budget().try_consume(7U);
        budget_after = context.budget().remaining();
        progress_accepted = context.progress().try_report(
            vna::runtime::RuntimeProgress{1U, 2U});
        return vna::runtime::RuntimeWorkStep::running();
    }

    vna::runtime::RuntimeWorkStep resume(
        vna::runtime::ExecutionContext& context) noexcept override {
        stop_requested_at_resume = context.stop().stop_requested();
        deadline_expired_at_resume = context.deadline().expired();
        budget_persisted = context.budget().remaining() == budget_after;
        return vna::runtime::RuntimeWorkStep::completed();
    }

    bool stop_requested_at_start{true};
    bool stop_requested_at_resume{false};
    bool deadline_enabled{false};
    bool deadline_expired_at_start{true};
    bool deadline_expired_at_resume{false};
    bool budget_consumed{false};
    bool progress_accepted{true};
    bool budget_persisted{false};
    std::uint64_t deadline_tick{0U};
    std::uint64_t budget_before{0U};
    std::uint64_t budget_after{0U};
};

class RecordingCompletionSink final : public vna::runtime::RuntimeCompletionSink {
public:
    void on_runtime_terminal(
        vna::runtime::WorkId value,
        vna::runtime::RuntimeTerminal result) noexcept override {
        ++terminals;
        work = value;
        terminal = result;
    }

    void on_runtime_drain_terminal(
        vna::runtime::WorkId value,
        vna::runtime::RuntimeDrainTerminal result) noexcept override {
        ++drain_terminals;
        drain_work = value;
        drain_terminal = result;
    }

    std::uint32_t terminals{0U};
    std::uint32_t drain_terminals{0U};
    vna::runtime::WorkId work{};
    vna::runtime::WorkId drain_work{};
    vna::runtime::RuntimeTerminal terminal{};
    vna::runtime::RuntimeDrainTerminal drain_terminal{};
};

class ReentrantTerminalSink final : public vna::runtime::RuntimeCompletionSink {
public:
    explicit ReentrantTerminalSink(
        vna::runtime::OperationRuntime& runtime,
        const vna::runtime::RuntimeCompletionReceiver& receiver) noexcept
        : runtime_(&runtime), receiver_(&receiver) {}

    void on_runtime_terminal(
        vna::runtime::WorkId work,
        vna::runtime::RuntimeTerminal terminal) noexcept override {
        (void)work;
        (void)terminal;
        ++terminals;
        if (terminals != 1U) {
            return;
        }
        reentrant_pump_did_work = runtime_->run_one(*receiver_, *this);
        auto reservation = runtime_->reserve_work(
            vna::runtime::WorkId{52U}, kExecutionLimits, *receiver_);
        capacity_reused_during_callback = reservation.has_value();
    }

    void on_runtime_drain_terminal(
        vna::runtime::WorkId work,
        vna::runtime::RuntimeDrainTerminal terminal) noexcept override {
        (void)work;
        (void)terminal;
    }

    std::uint32_t terminals{0U};
    bool reentrant_pump_did_work{true};
    bool capacity_reused_during_callback{true};

private:
    vna::runtime::OperationRuntime* runtime_{nullptr};
    const vna::runtime::RuntimeCompletionReceiver* receiver_{nullptr};
};

class ReentrantDrainTerminalSink final
    : public vna::runtime::RuntimeCompletionSink {
public:
    explicit ReentrantDrainTerminalSink(
        vna::runtime::OperationRuntime& runtime,
        const vna::runtime::RuntimeCompletionReceiver& receiver) noexcept
        : runtime_(&runtime), receiver_(&receiver) {}

    void on_runtime_terminal(
        vna::runtime::WorkId work,
        vna::runtime::RuntimeTerminal terminal) noexcept override {
        (void)work;
        ++work_terminals;
        if (terminal.kind != vna::runtime::RuntimeTerminalKind::Draining ||
            work_terminals != 1U) {
            return;
        }
        handoff_callback_active = true;
        reentrant_handoff_pump_did_work = runtime_->run_one(*receiver_, *this);
        handoff_callback_active = false;
    }

    void on_runtime_drain_terminal(
        vna::runtime::WorkId work,
        vna::runtime::RuntimeDrainTerminal terminal) noexcept override {
        (void)work;
        (void)terminal;
        ++drain_terminals;
        drain_arrived_during_handoff = handoff_callback_active;
        if (drain_terminals != 1U) {
            return;
        }
        reentrant_pump_did_work = runtime_->run_one(*receiver_, *this);
        auto reservation = runtime_->reserve_work(
            vna::runtime::WorkId{53U}, kExecutionLimits, *receiver_);
        capacity_reused_during_callback = reservation.has_value();
    }

    std::uint32_t work_terminals{0U};
    std::uint32_t drain_terminals{0U};
    bool reentrant_handoff_pump_did_work{true};
    bool drain_arrived_during_handoff{false};
    bool reentrant_pump_did_work{true};
    bool capacity_reused_during_callback{true};

private:
    vna::runtime::OperationRuntime* runtime_{nullptr};
    const vna::runtime::RuntimeCompletionReceiver* receiver_{nullptr};
    bool handoff_callback_active{false};
};

TEST(OperationRuntimeContract, UndispatchedReservationReturnsFixedCapacity) {
    using namespace vna::runtime;

    ManualRuntimeClock clock;
    OperationRuntime runtime{1U, clock};
    auto receiver = runtime.register_completion_receiver();
    {
        auto first = runtime.reserve_work(
            WorkId{1U}, kExecutionLimits, receiver);
        VNA_REQUIRE(first.has_value());
        auto reservation = std::move(first).take_value();
        VNA_REQUIRE(reservation.valid());
        VNA_REQUIRE(reservation.completion_reserved());
        VNA_REQUIRE(reservation.work_id() == WorkId{1U});

        auto second = runtime.reserve_work(
            WorkId{2U}, kExecutionLimits, receiver);
        VNA_REQUIRE(!second.has_value());
        VNA_REQUIRE(second.error().code == RuntimeErrc::ResourceExhausted);
        VNA_REQUIRE(runtime.inspect().reserved == 1U);
    }

    VNA_REQUIRE(runtime.inspect().reserved == 0U);
    auto after_release = runtime.reserve_work(
        WorkId{3U}, kExecutionLimits, receiver);
    VNA_REQUIRE(after_release.has_value());
}

TEST(OperationRuntimeContract, AdmissionRejectsUnboundedLimitsAndDuplicateWorkId) {
    using namespace vna::runtime;

    ManualRuntimeClock clock;
    OperationRuntime runtime{2U, clock};
    auto receiver = runtime.register_completion_receiver();

    auto zero_budget = runtime.reserve_work(
        WorkId{4U}, ExecutionLimits{100U, 0U}, receiver);
    VNA_REQUIRE(!zero_budget.has_value());
    VNA_REQUIRE(
        zero_budget.error().code == RuntimeErrc::InvalidExecutionLimits);
    VNA_REQUIRE(runtime.inspect().reserved == 0U);

    auto first = runtime.reserve_work(WorkId{4U}, kExecutionLimits, receiver);
    VNA_REQUIRE(first.has_value());
    auto duplicate = runtime.reserve_work(
        WorkId{4U}, kExecutionLimits, receiver);
    VNA_REQUIRE(!duplicate.has_value());
    VNA_REQUIRE(duplicate.error().code == RuntimeErrc::DuplicateWorkId);
    VNA_REQUIRE(runtime.inspect().reserved == 1U);
}

TEST(OperationRuntimeContract, ReservedDispatchIsNonInlineAndCompletesOnce) {
    using namespace vna::runtime;

    ManualRuntimeClock clock;
    OperationRuntime runtime{1U, clock};
    auto receiver = runtime.register_completion_receiver();
    CompletingWork work;
    ImmediateRuntimeWorkAdapter work_adapter{work};
    RecordingCompletionSink completion;
    auto reservation_result = runtime.reserve_work(
        WorkId{11U}, kExecutionLimits, receiver);
    VNA_REQUIRE(reservation_result.has_value());
    auto reservation = std::move(reservation_result).take_value();

    auto dispatch = runtime.dispatch(
        std::move(reservation),
        work_adapter);
    VNA_REQUIRE(dispatch.has_value());
    VNA_REQUIRE(work.executions == 0U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 1U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(work.executions == 1U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().running == 1U);
    VNA_REQUIRE(runtime.inspect().completed == 0U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.work == WorkId{11U});
    VNA_REQUIRE(completion.terminal.kind == RuntimeTerminalKind::Completed);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(runtime.inspect().completed == 1U);

    VNA_REQUIRE(!runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 1U);
}

TEST(OperationRuntimeContract, DeferredWorkKeepsCapacityUntilTrueTerminal) {
    using namespace vna::runtime;

    ManualRuntimeClock clock;
    OperationRuntime runtime{1U, clock};
    auto receiver = runtime.register_completion_receiver();
    DeferredWork work;
    RecordingCompletionSink completion;
    auto reservation_result = runtime.reserve_work(
        WorkId{21U}, kExecutionLimits, receiver);
    VNA_REQUIRE(reservation_result.has_value());
    auto reservation = std::move(reservation_result).take_value();

    auto dispatch = runtime.dispatch(
        std::move(reservation),
        work);
    VNA_REQUIRE(dispatch.has_value());
    VNA_REQUIRE(work.starts == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 1U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(work.starts == 1U);
    VNA_REQUIRE(work.resumes == 0U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().running == 1U);

    auto while_running = runtime.reserve_work(
        WorkId{22U}, kExecutionLimits, receiver);
    VNA_REQUIRE(!while_running.has_value());
    VNA_REQUIRE(while_running.error().code == RuntimeErrc::ResourceExhausted);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(work.resumes == 1U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().running == 1U);

    work.complete = true;
    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(work.resumes == 2U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().running == 1U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.terminal.kind == RuntimeTerminalKind::Completed);
    VNA_REQUIRE(runtime.inspect().running == 0U);
    VNA_REQUIRE(runtime.inspect().completed == 1U);

    VNA_REQUIRE(!runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 1U);
    auto after_terminal = runtime.reserve_work(
        WorkId{23U}, kExecutionLimits, receiver);
    VNA_REQUIRE(after_terminal.has_value());
}

TEST(OperationRuntimeContract, DrainingWorkKeepsCapacityUntilDrainTerminal) {
    using namespace vna::runtime;

    ManualRuntimeClock clock;
    OperationRuntime runtime{1U, clock};
    auto receiver = runtime.register_completion_receiver();
    DrainingWork work;
    RecordingCompletionSink completion;
    auto reservation_result = runtime.reserve_work(
        WorkId{31U}, kExecutionLimits, receiver);
    VNA_REQUIRE(reservation_result.has_value());
    auto reservation = std::move(reservation_result).take_value();

    auto dispatch = runtime.dispatch(
        std::move(reservation),
        work);
    VNA_REQUIRE(dispatch.has_value());

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(runtime.inspect().running == 1U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().running == 1U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.terminal.kind == RuntimeTerminalKind::Draining);
    VNA_REQUIRE(completion.terminal.drain == DrainId{91U});
    VNA_REQUIRE(completion.drain_terminals == 0U);
    VNA_REQUIRE(runtime.inspect().running == 0U);
    VNA_REQUIRE(runtime.inspect().draining == 1U);
    VNA_REQUIRE(runtime.inspect().completed == 0U);

    auto while_draining = runtime.reserve_work(
        WorkId{32U}, kExecutionLimits, receiver);
    VNA_REQUIRE(!while_draining.has_value());
    VNA_REQUIRE(while_draining.error().code == RuntimeErrc::ResourceExhausted);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(work.drain_resumes == 1U);
    VNA_REQUIRE(completion.drain_terminals == 0U);
    VNA_REQUIRE(runtime.inspect().draining == 1U);

    work.drain_complete = true;
    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(work.drain_resumes == 2U);
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.drain_terminals == 0U);
    VNA_REQUIRE(runtime.inspect().draining == 1U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.drain_terminals == 1U);
    VNA_REQUIRE(completion.drain_work == WorkId{31U});
    VNA_REQUIRE(
        completion.drain_terminal.kind == RuntimeDrainTerminalKind::Drained);
    VNA_REQUIRE(completion.drain_terminal.drain == DrainId{91U});
    VNA_REQUIRE(runtime.inspect().draining == 0U);
    VNA_REQUIRE(runtime.inspect().completed == 1U);

    VNA_REQUIRE(!runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.drain_terminals == 1U);
    auto after_drain = runtime.reserve_work(
        WorkId{33U}, kExecutionLimits, receiver);
    VNA_REQUIRE(after_drain.has_value());
}

TEST(OperationRuntimeContract, ExecutionContextCapabilitiesPersistAcrossPumps) {
    using namespace vna::runtime;

    ManualRuntimeClock clock;
    clock.advance(100U);
    OperationRuntime runtime{1U, clock};
    auto receiver = runtime.register_completion_receiver();
    ContextObservingWork work;
    RecordingCompletionSink completion;
    constexpr ExecutionLimits limits{105U, 10U};
    auto reservation_result = runtime.reserve_work(
        WorkId{41U}, limits, receiver);
    VNA_REQUIRE(reservation_result.has_value());
    auto reservation = std::move(reservation_result).take_value();

    auto dispatch = runtime.dispatch(
        std::move(reservation),
        work);
    VNA_REQUIRE(dispatch.has_value());

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(!work.stop_requested_at_start);
    VNA_REQUIRE(work.deadline_enabled);
    VNA_REQUIRE(work.deadline_tick == 105U);
    VNA_REQUIRE(!work.deadline_expired_at_start);
    VNA_REQUIRE(work.budget_consumed);
    VNA_REQUIRE(work.budget_before == 10U);
    VNA_REQUIRE(work.budget_after == work.budget_before - 7U);
    VNA_REQUIRE(!work.progress_accepted);
    VNA_REQUIRE(completion.terminals == 0U);

    clock.advance(5U);
    VNA_REQUIRE(runtime.request_stop(WorkId{41U}));
    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(work.stop_requested_at_resume);
    VNA_REQUIRE(work.deadline_expired_at_resume);
    VNA_REQUIRE(work.budget_persisted);
    VNA_REQUIRE(completion.terminals == 0U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.terminal.kind == RuntimeTerminalKind::Completed);
}

TEST(OperationRuntimeContract, WorkTerminalCallbackCannotRepumpOrReuseSlot) {
    using namespace vna::runtime;

    ManualRuntimeClock clock;
    OperationRuntime runtime{1U, clock};
    auto receiver = runtime.register_completion_receiver();
    CompletingWork work;
    ImmediateRuntimeWorkAdapter work_adapter{work};
    ReentrantTerminalSink completion{runtime, receiver};
    auto reservation_result = runtime.reserve_work(
        WorkId{51U}, kExecutionLimits, receiver);
    VNA_REQUIRE(reservation_result.has_value());
    auto reservation = std::move(reservation_result).take_value();

    auto dispatch = runtime.dispatch(
        std::move(reservation),
        work_adapter);
    VNA_REQUIRE(dispatch.has_value());

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 0U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(!completion.reentrant_pump_did_work);
    VNA_REQUIRE(!completion.capacity_reused_during_callback);
    VNA_REQUIRE(runtime.inspect().completed == 1U);

    auto after_callback = runtime.reserve_work(
        WorkId{54U}, kExecutionLimits, receiver);
    VNA_REQUIRE(after_callback.has_value());
}

TEST(OperationRuntimeContract, DrainTerminalCallbackCannotRepumpOrReuseSlot) {
    using namespace vna::runtime;

    ManualRuntimeClock clock;
    OperationRuntime runtime{1U, clock};
    auto receiver = runtime.register_completion_receiver();
    DrainingWork work;
    ReentrantDrainTerminalSink completion{runtime, receiver};
    auto reservation_result = runtime.reserve_work(
        WorkId{55U}, kExecutionLimits, receiver);
    VNA_REQUIRE(reservation_result.has_value());
    auto reservation = std::move(reservation_result).take_value();

    auto dispatch = runtime.dispatch(
        std::move(reservation),
        work);
    VNA_REQUIRE(dispatch.has_value());
    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.work_terminals == 0U);
    work.drain_complete = true;
    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.work_terminals == 1U);
    VNA_REQUIRE(!completion.reentrant_handoff_pump_did_work);
    VNA_REQUIRE(!completion.drain_arrived_during_handoff);
    VNA_REQUIRE(completion.drain_terminals == 0U);

    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.drain_terminals == 0U);
    VNA_REQUIRE(runtime.run_one(receiver, completion));
    VNA_REQUIRE(completion.drain_terminals == 1U);
    VNA_REQUIRE(!completion.reentrant_pump_did_work);
    VNA_REQUIRE(!completion.capacity_reused_during_callback);
    VNA_REQUIRE(runtime.inspect().completed == 1U);

    auto after_callback = runtime.reserve_work(
        WorkId{56U}, kExecutionLimits, receiver);
    VNA_REQUIRE(after_callback.has_value());
}

}  // namespace
