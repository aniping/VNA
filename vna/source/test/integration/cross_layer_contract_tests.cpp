#include "test_support.h"

#include "runtime/function/instrument/sweep_admission.h"

#include <cstdint>

namespace {

constexpr vna::runtime::ExecutionLimits kExecutionLimits{1000U, 64U};

class ManualRuntimeClock final : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return 0U; }
};

class CompletingSweepWork final : public vna::runtime::ImmediateRuntimeWork {
public:
    vna::runtime::RuntimeTerminal execute() noexcept override {
        ++executions;
        return vna::runtime::RuntimeTerminal{
            vna::runtime::RuntimeTerminalKind::Completed};
    }

    std::uint32_t executions{0U};
};

class DeferredSweepWork final : public vna::runtime::RuntimeWork {
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

class DrainingSweepWork final : public vna::runtime::RuntimeWork {
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
            vna::runtime::DrainId{191U});
    }

    vna::runtime::RuntimeDrainStep resume_drain(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        return vna::runtime::RuntimeDrainStep::running();
    }
};

class RecordingSweepCompletion final : public vna::instrument::SweepCompletionSink {
public:
    void on_sweep_terminal(
        vna::store::OperationSnapshot value) noexcept override {
        ++terminals;
        operation = value;
    }

    std::uint32_t terminals{0U};
    vna::store::OperationSnapshot operation{};
};

TEST(CrossLayerContract, AcceptedCommitPrecedesDispatchAndCompletionCommit) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    instrument::SweepAdmissionController controller{runtime, store};
    CompletingSweepWork work;
    runtime::ImmediateRuntimeWorkAdapter work_adapter{work};
    RecordingSweepCompletion completion;

    auto submitted = controller.submit(
        store::OperationId{51U},
        runtime::WorkId{61U},
        kExecutionLimits,
        work_adapter,
        completion);

    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(store.inspect_operation(store::OperationId{51U}).has_value());
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{51U})->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(runtime.inspect().queued == 1U);
    VNA_REQUIRE(work.executions == 0U);
    VNA_REQUIRE(completion.terminals == 0U);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(work.executions == 1U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{51U})->state ==
        store::OperationState::Accepted);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.operation.id == store::OperationId{51U});
    VNA_REQUIRE(completion.operation.state == store::OperationState::Completed);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{51U})->state ==
        store::OperationState::Completed);
}

TEST(CrossLayerContract, FailedInitialCommitReleasesAllOwnersAndNeverDispatches) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    instrument::SweepAdmissionController controller{runtime, store};
    CompletingSweepWork work;
    runtime::ImmediateRuntimeWorkAdapter work_adapter{work};
    RecordingSweepCompletion completion;

    auto submitted = controller.submit(
        store::OperationId{},
        runtime::WorkId{62U},
        kExecutionLimits,
        work_adapter,
        completion);

    VNA_REQUIRE(!submitted.has_value());
    VNA_REQUIRE(
        submitted.error().code ==
        instrument::SweepAdmissionErrc::StoreInitialCommitRejected);
    VNA_REQUIRE(store.inspect().visible_operations == 0U);
    VNA_REQUIRE(store.inspect().reserved_lifecycles == 0U);
    VNA_REQUIRE(runtime.inspect().reserved == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(!controller.run_one());
    VNA_REQUIRE(work.executions == 0U);
    VNA_REQUIRE(completion.terminals == 0U);
}

TEST(CrossLayerContract, AcceptedOperationRemainsVisibleWhileWorkIsRunning) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    instrument::SweepAdmissionController controller{runtime, store};
    DeferredSweepWork work;
    RecordingSweepCompletion completion;

    auto submitted = controller.submit(
        store::OperationId{71U},
        runtime::WorkId{81U},
        kExecutionLimits,
        work,
        completion);
    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(work.starts == 0U);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(work.starts == 1U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().running == 1U);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Accepted);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(work.resumes == 1U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Accepted);

    work.complete = true;
    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(work.resumes == 2U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Accepted);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.operation.state == store::OperationState::Completed);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Completed);
}

TEST(CrossLayerContract, ActiveDrainRejectsReuseOfItsWorkIdBeforeAccepted) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{2U, clock};
    store::InstrumentStore store{2U};
    instrument::SweepAdmissionController controller{runtime, store};
    DrainingSweepWork draining_work;
    RecordingSweepCompletion draining_completion;

    auto first = controller.submit(
        store::OperationId{72U},
        runtime::WorkId{82U},
        kExecutionLimits,
        draining_work,
        draining_completion);
    VNA_REQUIRE(first.has_value());
    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(runtime.inspect().draining == 1U);

    CompletingSweepWork duplicate_work;
    runtime::ImmediateRuntimeWorkAdapter duplicate_adapter{duplicate_work};
    RecordingSweepCompletion duplicate_completion;
    auto duplicate = controller.submit(
        store::OperationId{73U},
        runtime::WorkId{82U},
        kExecutionLimits,
        duplicate_adapter,
        duplicate_completion);

    VNA_REQUIRE(!duplicate.has_value());
    VNA_REQUIRE(
        duplicate.error().code == instrument::SweepAdmissionErrc::DuplicateWorkId);
    VNA_REQUIRE(!store.inspect_operation(store::OperationId{73U}).has_value());
    VNA_REQUIRE(duplicate_work.executions == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(runtime.inspect().draining == 1U);
}

TEST(CrossLayerContract, ControllerCannotConsumeAnotherControllersCompletion) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore first_store{1U};
    store::InstrumentStore second_store{1U};
    instrument::SweepAdmissionController first_controller{runtime, first_store};
    instrument::SweepAdmissionController second_controller{runtime, second_store};
    CompletingSweepWork work;
    runtime::ImmediateRuntimeWorkAdapter work_adapter{work};
    RecordingSweepCompletion completion;

    auto submitted = second_controller.submit(
        store::OperationId{91U},
        runtime::WorkId{92U},
        kExecutionLimits,
        work_adapter,
        completion);
    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(second_controller.run_one());
    VNA_REQUIRE(completion.terminals == 0U);

    VNA_REQUIRE(!first_controller.run_one());
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(
        second_store.inspect_operation(store::OperationId{91U})->state ==
        store::OperationState::Accepted);

    VNA_REQUIRE(second_controller.run_one());
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(
        second_store.inspect_operation(store::OperationId{91U})->state ==
        store::OperationState::Completed);
}

}  // namespace
