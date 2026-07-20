#include "test_support.h"

#include "adapter/mock/mock_board.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/instrument/instrument_kernel.h"
#include "store_contract_test_access.h"

#include <cstdint>
#include <utility>

namespace {

class RecoveryRuntimeClock final : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return 0U; }
};

vna::board::MockScenario successful_scenario() noexcept {
    vna::board::MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_duration = 350U;
    scenario.incident_a[0U] = vna::board::ComplexSample{1.0F, 0.25F};
    scenario.incident_a[1U] = vna::board::ComplexSample{2.0F, -0.5F};
    scenario.incident_a[2U] = vna::board::ComplexSample{3.0F, 0.75F};
    scenario.response_b[0U] = vna::board::ComplexSample{0.5F, -0.25F};
    scenario.response_b[1U] = vna::board::ComplexSample{1.5F, 0.5F};
    scenario.response_b[2U] = vna::board::ComplexSample{2.5F, -0.75F};
    scenario.incident_quality.flags = static_cast<std::uint32_t>(
        vna::board::ReceiverQualityFlag::SourceUnleveled);
    return scenario;
}

vna::board::MockOpenedBoard open_mock(
    vna::board::MockBoardProvider& provider) {
    auto opened = provider.open_controlled(vna::board::BoardOpenRequest{
        1U, vna::board::BoardContractVersion{1U, 0U}});
    return std::move(opened).take_value();
}

class StoreRecoveryHarness final {
public:
    explicit StoreRecoveryHarness(std::size_t store_capacity)
        : provider{
              vna::board::MockCapabilityProfile{201U}, successful_scenario()},
          opened{open_mock(provider)},
          runtime{1U, clock},
          store{store_capacity},
          acquisition_resources{1U},
          kernel{
              runtime,
              store,
              opened.board.execution(),
              acquisition_resources,
              clock,
              vna::instrument::AOnlyKernelProfile{1000U, 64U}} {}

    /// 推进到 Runtime 已持有成功 completion、但 L2 尚未提交 candidate 的边界。
    /// @param operation 返回新建且此时仍为 Accepted 的 OperationId。
    void drive_to_invisible_candidate(vna::store::OperationId& operation) {
        const auto submitted = kernel.submit_a_only(
            vna::instrument::AOnlySweepRequest{
                3U,
                1.0e6,
                3.0e6,
                vna::instrument::AOnlyDiagnosticAuthorization::
                    issue_for_mock_diagnostics()});
        VNA_REQUIRE(submitted.has_value());
        operation = submitted.value().operation;
        VNA_REQUIRE(kernel.run_one());
        opened.control->advance(1U);
        VNA_REQUIRE(kernel.run_one());
        opened.control->advance(350U);
        VNA_REQUIRE(kernel.run_one());
        VNA_REQUIRE(
            store.inspect_operation(operation)->state ==
            vna::store::OperationState::Accepted);
        VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());
    }

    RecoveryRuntimeClock clock{};
    vna::board::MockBoardProvider provider;
    vna::board::MockOpenedBoard opened;
    vna::runtime::OperationRuntime runtime;
    vna::store::InstrumentStore store;
    vna::acquisition::AcquisitionAdmissionPool acquisition_resources;
    vna::instrument::InstrumentKernel kernel;
};

void require_atomic_failed_publication(
    StoreRecoveryHarness& harness,
    vna::store::OperationId operation) {
    using namespace vna;

    const auto operation_snapshot = harness.store.inspect_operation(operation);
    const auto fence = harness.store.inspect_fence(operation);
    const auto status = harness.store.inspect_status();
    const auto event = harness.store.latest_event();
    VNA_REQUIRE(operation_snapshot.has_value());
    VNA_REQUIRE(fence.has_value());
    VNA_REQUIRE(event.has_value());
    VNA_REQUIRE(operation_snapshot->state == store::OperationState::Failed);
    VNA_REQUIRE(fence->state == store::OperationState::Failed);
    VNA_REQUIRE(status.state == store::OperationState::Failed);
    VNA_REQUIRE(event->state == store::OperationState::Failed);
    VNA_REQUIRE(operation_snapshot->revision == fence->revision);
    VNA_REQUIRE(operation_snapshot->revision == status.revision);
    VNA_REQUIRE(operation_snapshot->revision == event->revision);
    VNA_REQUIRE(event->has_acquisition_failure);
    VNA_REQUIRE(
        event->failure.phase ==
        acquisition::AcquisitionFailurePhase::PublicationCommit);
    VNA_REQUIRE(
        event->failure.reason ==
        acquisition::AcquisitionFailureReason::StoreCommitRejected);
    VNA_REQUIRE(!event->has_completed_sweep);
    VNA_REQUIRE(!harness.store.inspect_completed_sweep(operation).has_value());
}

TEST(StoreCommitRecoveryContract, ValidationRejectsAllSuccessFactsThenRecovers) {
    using namespace vna;

    StoreRecoveryHarness harness{2U};
    store::OperationId rejected_operation{};
    harness.drive_to_invisible_candidate(rejected_operation);
    VNA_REQUIRE(harness.acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().failure_finalizations == 0U);
    store::InstrumentStoreContractTestAccess::
        reject_next_completed_sweep_validation(harness.store);

    // 一个 L2 completion 回合必须完成 success 回滚和 state-only Failed 提交。
    VNA_REQUIRE(harness.kernel.run_one());
    require_atomic_failed_publication(harness, rejected_operation);
    VNA_REQUIRE(harness.store.inspect_publications().completed_sweeps == 0U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().failure_finalizations == 1U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().success_finalizations == 0U);
    VNA_REQUIRE(
        harness.kernel.inspect_integrity().state ==
        instrument::InstrumentIntegrityState::Healthy);

    // 后续成功扫描证明 candidate payload、Board execution 与上层 owner 均已归还。
    store::OperationId recovered_operation{};
    harness.drive_to_invisible_candidate(recovered_operation);
    VNA_REQUIRE(harness.kernel.run_one());
    VNA_REQUIRE(
        harness.store.inspect_operation(recovered_operation)->state ==
        store::OperationState::Completed);
    VNA_REQUIRE(
        harness.store.inspect_completed_sweep(recovered_operation).has_value());
    VNA_REQUIRE(harness.store.inspect_publications().completed_sweeps == 1U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().success_finalizations == 1U);
}

TEST(StoreCommitRecoveryContract, WriteRejectPreservesImmutableHistory) {
    using namespace vna;

    StoreRecoveryHarness harness{2U};
    store::OperationId historical_operation{};
    harness.drive_to_invisible_candidate(historical_operation);
    VNA_REQUIRE(harness.kernel.run_one());
    const auto historical =
        harness.store.inspect_completed_sweep(historical_operation);
    VNA_REQUIRE(historical.has_value());
    const auto historical_value = historical->observation(0U).values[1U];
    const auto historical_quality =
        historical->observation(0U).quality_flags[1U];
    const auto historical_run = historical->board_evidence(0U).run_id;

    store::OperationId rejected_operation{};
    harness.drive_to_invisible_candidate(rejected_operation);
    store::InstrumentStoreContractTestAccess::
        reject_next_completed_sweep_write(harness.store);
    VNA_REQUIRE(harness.kernel.run_one());
    require_atomic_failed_publication(harness, rejected_operation);

    const auto after_failure =
        harness.store.inspect_completed_sweep(historical_operation);
    VNA_REQUIRE(after_failure.has_value());
    VNA_REQUIRE(after_failure->id() == historical->id());
    VNA_REQUIRE(after_failure->revision() == historical->revision());
    VNA_REQUIRE(
        after_failure->observation(0U).values[1U] == historical_value);
    VNA_REQUIRE(
        after_failure->observation(0U).quality_flags[1U] == historical_quality);
    VNA_REQUIRE(after_failure->board_evidence(0U).run_id == historical_run);
    VNA_REQUIRE(harness.store.inspect_publications().completed_sweeps == 1U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().failure_finalizations == 1U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().success_finalizations == 1U);
    VNA_REQUIRE(
        harness.kernel.inspect_integrity().state ==
        instrument::InstrumentIntegrityState::Healthy);
}

TEST(StoreCommitRecoveryContract, IntegrityFaultEntersTypedFailStop) {
    using namespace vna;

    StoreRecoveryHarness harness{1U};
    store::OperationId operation{};
    harness.drive_to_invisible_candidate(operation);
    store::InstrumentStoreContractTestAccess::
        reject_next_completed_sweep_validation(harness.store);
    store::InstrumentStoreContractTestAccess::
        fail_next_acquisition_failure_commit(harness.store);

    VNA_REQUIRE(harness.kernel.run_one());
    VNA_REQUIRE(
        harness.store.inspect_operation(operation)->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(!harness.store.inspect_completed_sweep(operation).has_value());
    VNA_REQUIRE(!harness.store.inspect_fence(operation).has_value());
    VNA_REQUIRE(!harness.store.latest_event().has_value());
    VNA_REQUIRE(harness.acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().failure_finalizations == 0U);

    const auto integrity = harness.kernel.inspect_integrity();
    VNA_REQUIRE(
        integrity.state == instrument::InstrumentIntegrityState::StoreFailStop);
    VNA_REQUIRE(integrity.operation == operation);
    VNA_REQUIRE(integrity.has_store_error);
    VNA_REQUIRE(integrity.store_error.code == store::StoreErrc::IntegrityFault);

    const auto board_before = harness.opened.control->observations();
    const auto rejected = harness.kernel.submit_a_only(
        instrument::AOnlySweepRequest{
            3U,
            1.0e6,
            3.0e6,
            instrument::AOnlyDiagnosticAuthorization::
                issue_for_mock_diagnostics()});
    VNA_REQUIRE(!rejected.has_value());
    VNA_REQUIRE(
        rejected.error().code == instrument::AOnlySubmitErrc::InstrumentFailStop);
    const auto board_after = harness.opened.control->observations();
    VNA_REQUIRE(
        board_after.acquired_execution_reservations ==
        board_before.acquired_execution_reservations);
}

TEST(StoreCommitRecoveryContract, MalformedFailedReceiptCannotReleaseOwners) {
    using namespace vna;

    StoreRecoveryHarness harness{1U};
    store::OperationId operation{};
    harness.drive_to_invisible_candidate(operation);
    store::InstrumentStoreContractTestAccess::
        reject_next_completed_sweep_validation(harness.store);
    store::InstrumentStoreContractTestAccess::
        return_malformed_acquisition_failure_receipt(harness.store);

    VNA_REQUIRE(harness.kernel.run_one());
    VNA_REQUIRE(
        harness.store.inspect_operation(operation)->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(!harness.store.inspect_fence(operation).has_value());
    VNA_REQUIRE(!harness.store.latest_event().has_value());
    VNA_REQUIRE(harness.acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(harness.acquisition_resources.inspect().failure_finalizations == 0U);

    const auto integrity = harness.kernel.inspect_integrity();
    VNA_REQUIRE(
        integrity.state == instrument::InstrumentIntegrityState::StoreFailStop);
    VNA_REQUIRE(integrity.operation == operation);
    VNA_REQUIRE(integrity.has_store_error);
    VNA_REQUIRE(integrity.store_error.code == store::StoreErrc::IntegrityFault);
}

}  // namespace
