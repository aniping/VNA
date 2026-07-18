#include "test_support.h"

#include "runtime/function/instrument/sweep_admission.h"

#include <cstdint>

namespace {

class CompletingSweepWork final : public vna::runtime::RuntimeWork {
public:
    vna::runtime::RuntimeTerminal execute() noexcept override {
        ++executions;
        return vna::runtime::RuntimeTerminal{
            vna::runtime::RuntimeTerminalKind::Completed};
    }

    std::uint32_t executions{0U};
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

    runtime::OperationRuntime runtime{1U};
    store::InstrumentStore store{1U};
    instrument::SweepAdmissionController controller{runtime, store};
    CompletingSweepWork work;
    RecordingSweepCompletion completion;

    auto submitted = controller.submit(
        store::OperationId{51U},
        runtime::WorkId{61U},
        work,
        completion);

    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(store.inspect_operation(store::OperationId{51U}).has_value());
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{51U})->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(runtime.inspect().queued == 1U);
    VNA_REQUIRE(work.executions == 0U);
    VNA_REQUIRE(completion.terminals == 0U);

    VNA_REQUIRE(runtime.run_one());
    VNA_REQUIRE(work.executions == 1U);
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.operation.id == store::OperationId{51U});
    VNA_REQUIRE(completion.operation.state == store::OperationState::Completed);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{51U})->state ==
        store::OperationState::Completed);
}

TEST(CrossLayerContract, FailedInitialCommitReleasesAllOwnersAndNeverDispatches) {
    using namespace vna;

    runtime::OperationRuntime runtime{1U};
    store::InstrumentStore store{1U};
    instrument::SweepAdmissionController controller{runtime, store};
    CompletingSweepWork work;
    RecordingSweepCompletion completion;

    auto submitted = controller.submit(
        store::OperationId{},
        runtime::WorkId{62U},
        work,
        completion);

    VNA_REQUIRE(!submitted.has_value());
    VNA_REQUIRE(
        submitted.error().code ==
        instrument::SweepAdmissionErrc::StoreInitialCommitRejected);
    VNA_REQUIRE(store.inspect().visible_operations == 0U);
    VNA_REQUIRE(store.inspect().reserved_lifecycles == 0U);
    VNA_REQUIRE(runtime.inspect().reserved == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(!runtime.run_one());
    VNA_REQUIRE(work.executions == 0U);
    VNA_REQUIRE(completion.terminals == 0U);
}

}  // namespace
