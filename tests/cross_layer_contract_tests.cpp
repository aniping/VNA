#include "test_support.hpp"

#include "vna/instrument/sweep_admission.hpp"

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

void accepted_commit_precedes_dispatch_and_completion_commit() {
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

void failed_initial_commit_releases_all_owners_and_never_dispatches() {
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

int main() {
    int failures = 0;
    failures += vna::test::run(
        "accepted commit precedes dispatch and completion commit",
        accepted_commit_precedes_dispatch_and_completion_commit);
    failures += vna::test::run(
        "failed initial commit releases all owners and never dispatches",
        failed_initial_commit_releases_all_owners_and_never_dispatches);
    return failures;
}
