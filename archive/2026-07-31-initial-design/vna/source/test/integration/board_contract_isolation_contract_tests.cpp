#include "test_support.h"

#include "adapter/mock/mock_board.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/instrument/instrument_kernel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>

namespace {

class IsolationRuntimeClock final
    : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return 0U; }
};

vna::board::MockScenario healthy_scenario() {
    using namespace vna::board;

    MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_duration = 350U;
    for (std::size_t index = 0U; index < 6U; ++index) {
        scenario.incident_a[index] = ComplexSample{
            1.0F + static_cast<float>(index),
            0.1F * static_cast<float>(index)};
        scenario.response_b[index] = ComplexSample{
            11.0F + static_cast<float>(index),
            -0.1F * static_cast<float>(index)};
    }
    return scenario;
}

vna::instrument::AOnlySweepRequest request() noexcept {
    return vna::instrument::AOnlySweepRequest{
        6U,
        1.0e6,
        6.0e6,
        vna::instrument::AOnlyDiagnosticAuthorization::
            issue_for_mock_diagnostics()};
}

class IsolationHarness final {
public:
    explicit IsolationHarness(vna::board::MockBoardProvider& provider)
        : opened_(open(provider)),
          runtime_(1U, clock_),
          store_(4U),
          resources_(1U),
          kernel_(
              runtime_,
              store_,
              opened_.board.execution(),
              resources_,
              clock_,
              vna::instrument::AOnlyKernelProfile{1000U, 64U}) {}

    bool run(
        const vna::board::MockScenario& scenario,
        vna::store::OperationId& operation) {
        opened_.control->load_scenario(scenario);
        const auto submission = kernel_.submit_a_only(request());
        if (!submission.has_value()) {
            return false;
        }
        operation = submission.value().operation;
        if (!kernel_.run_one()) {
            return false;
        }
        opened_.control->advance(1U);
        if (!kernel_.run_one()) {
            return false;
        }
        opened_.control->advance(350U);
        return kernel_.run_one() && kernel_.run_one();
    }

    vna::instrument::AOnlySubmitResult submit_again() noexcept {
        return kernel_.submit_a_only(request());
    }

    vna::board::BoardSessionId session_id() const noexcept {
        return opened_.board.initial_capabilities().session_id;
    }

    vna::board::MockBoardControl& control() noexcept {
        return *opened_.control;
    }

    vna::store::InstrumentStore& store() noexcept { return store_; }

private:
    static vna::board::MockOpenedBoard open(
        vna::board::MockBoardProvider& provider) {
        auto result = provider.open_controlled(vna::board::BoardOpenRequest{
            1U, vna::board::BoardContractVersion{1U, 0U}});
        if (!result.has_value()) {
            std::terminate();
        }
        return std::move(result).take_value();
    }

    vna::board::MockOpenedBoard opened_;
    IsolationRuntimeClock clock_;
    vna::runtime::OperationRuntime runtime_;
    vna::store::InstrumentStore store_;
    vna::acquisition::AcquisitionAdmissionPool resources_;
    vna::instrument::InstrumentKernel kernel_;
};

struct ExpectedViolation final {
    vna::board::MockRunContractFault fault{
        vna::board::MockRunContractFault::None};
    vna::board::BoardContractViolationKind kind{
        vna::board::BoardContractViolationKind::WrongManifest};
    vna::board::BoardRunCallbackKind callback{
        vna::board::BoardRunCallbackKind::Chunk};
    bool wrong_manifest{false};
    bool wrong_prepared{false};
    bool wrong_run{false};
    bool wrong_generation{false};
    std::uint32_t chunk_callbacks{0U};
    std::uint32_t terminal_callbacks{0U};
};

TEST(BoardContractIsolationContract,
     IsolatesEveryFaultySessionAndRecoversOnlyAfterReopen) {
    using namespace vna;

    const std::array<ExpectedViolation, 6U> cases{
        ExpectedViolation{
            board::MockRunContractFault::WrongManifest,
            board::BoardContractViolationKind::WrongManifest,
            board::BoardRunCallbackKind::Chunk,
            true, false, false, false, 1U, 1U},
        ExpectedViolation{
            board::MockRunContractFault::WrongPreparedExecution,
            board::BoardContractViolationKind::WrongPreparedExecution,
            board::BoardRunCallbackKind::Chunk,
            false, true, false, false, 1U, 1U},
        ExpectedViolation{
            board::MockRunContractFault::WrongBoardRunId,
            board::BoardContractViolationKind::WrongBoardRunId,
            board::BoardRunCallbackKind::Chunk,
            false, false, true, false, 1U, 1U},
        ExpectedViolation{
            board::MockRunContractFault::WrongGeneration,
            board::BoardContractViolationKind::WrongGeneration,
            board::BoardRunCallbackKind::Chunk,
            false, false, false, true, 1U, 1U},
        ExpectedViolation{
            board::MockRunContractFault::MultipleTerminal,
            board::BoardContractViolationKind::MultipleTerminal,
            board::BoardRunCallbackKind::Terminal,
            false, false, false, false, 2U, 2U},
        ExpectedViolation{
            board::MockRunContractFault::CallbackAfterTerminal,
            board::BoardContractViolationKind::CallbackAfterTerminal,
            board::BoardRunCallbackKind::Chunk,
            false, false, false, false, 3U, 1U}};

    for (const auto& expected : cases) {
        SCOPED_TRACE(static_cast<int>(expected.kind));
        board::MockBoardProvider provider{
            board::MockCapabilityProfile{201U}, healthy_scenario()};
        board::BoardSessionId isolated_session{};

        {
            IsolationHarness harness{provider};
            isolated_session = harness.session_id();
            auto scenario = healthy_scenario();
            scenario.contract_fault = expected.fault;

            store::OperationId operation{};
            VNA_REQUIRE(harness.run(scenario, operation));
            const auto operation_snapshot =
                harness.store().inspect_operation(operation);
            const auto fence = harness.store().inspect_fence(operation);
            const auto status = harness.store().inspect_status();
            const auto event = harness.store().latest_event();
            VNA_REQUIRE(operation_snapshot.has_value());
            VNA_REQUIRE(fence.has_value());
            VNA_REQUIRE(event.has_value());
            VNA_REQUIRE(
                operation_snapshot->state == store::OperationState::Failed);
            VNA_REQUIRE(fence->state == store::OperationState::Failed);
            VNA_REQUIRE(status.state == store::OperationState::Failed);
            VNA_REQUIRE(event->state == store::OperationState::Failed);
            VNA_REQUIRE(operation_snapshot->revision == fence->revision);
            VNA_REQUIRE(operation_snapshot->revision == status.revision);
            VNA_REQUIRE(operation_snapshot->revision == event->revision);
            VNA_REQUIRE(event->operation == operation);
            VNA_REQUIRE(event->has_acquisition_failure);
            VNA_REQUIRE(!event->has_completed_sweep);
            VNA_REQUIRE(
                !harness.store().inspect_completed_sweep(operation).has_value());
            VNA_REQUIRE(
                harness.store().inspect_publications().completed_sweeps == 0U);

            const auto& failure = event->failure;
            VNA_REQUIRE(
                failure.phase ==
                acquisition::AcquisitionFailurePhase::Run);
            VNA_REQUIRE(
                failure.reason ==
                acquisition::AcquisitionFailureReason::
                    BoardContractViolation);
            VNA_REQUIRE(
                failure.retry ==
                acquisition::AcquisitionRetryClass::AfterRecovery);
            VNA_REQUIRE(
                failure.safety ==
                acquisition::AcquisitionSafetyImpact::RunTerminalObserved);
            VNA_REQUIRE(failure.manifest.valid());
            VNA_REQUIRE(failure.prepared.valid());
            VNA_REQUIRE(failure.run.valid());
            VNA_REQUIRE(failure.generation.valid());
            VNA_REQUIRE(failure.board_session == isolated_session);
            VNA_REQUIRE(!failure.has_observation_error);
            VNA_REQUIRE(failure.has_contract_violation);

            const auto& violation = failure.contract_violation;
            VNA_REQUIRE(violation.kind == expected.kind);
            VNA_REQUIRE(violation.callback == expected.callback);
            VNA_REQUIRE(violation.expected_manifest == failure.manifest);
            VNA_REQUIRE(violation.expected_prepared == failure.prepared);
            VNA_REQUIRE(violation.expected_run == failure.run);
            VNA_REQUIRE(
                violation.expected_generation == failure.generation);
            VNA_REQUIRE(
                violation.has_observed_manifest ==
                (expected.callback == board::BoardRunCallbackKind::Chunk));
            VNA_REQUIRE(
                violation.has_observed_prepared ==
                (expected.callback == board::BoardRunCallbackKind::Chunk));
            VNA_REQUIRE(violation.has_observed_run);
            VNA_REQUIRE(violation.has_observed_generation);
            if (violation.has_observed_manifest) {
                VNA_REQUIRE(
                    (violation.observed_manifest !=
                     violation.expected_manifest) == expected.wrong_manifest);
            } else {
                VNA_REQUIRE(!violation.observed_manifest.valid());
            }
            if (violation.has_observed_prepared) {
                VNA_REQUIRE(
                    (violation.observed_prepared !=
                     violation.expected_prepared) == expected.wrong_prepared);
            } else {
                VNA_REQUIRE(!violation.observed_prepared.valid());
            }
            VNA_REQUIRE(
                (violation.observed_run != violation.expected_run) ==
                expected.wrong_run);
            VNA_REQUIRE(
                (violation.observed_generation !=
                 violation.expected_generation) == expected.wrong_generation);
            VNA_REQUIRE(
                violation.observed_terminal_callbacks ==
                expected.terminal_callbacks);

            const auto observations = harness.control().observations();
            VNA_REQUIRE(observations.accepted_prepare_calls == 1U);
            VNA_REQUIRE(observations.accepted_run_calls == 1U);
            VNA_REQUIRE(
                observations.run_chunk_callbacks == expected.chunk_callbacks);
            VNA_REQUIRE(
                observations.consumed_chunk_payloads ==
                expected.chunk_callbacks);
            VNA_REQUIRE(
                observations.run_terminal_callbacks ==
                expected.terminal_callbacks);
            VNA_REQUIRE(observations.released_execution_reservations == 1U);
            VNA_REQUIRE(observations.isolated_session_transitions == 1U);
            VNA_REQUIRE(
                harness.control().session_state() ==
                board::MockSessionState::IsolatedContractViolation);

            const auto previous_event = *event;
            const auto rejected = harness.submit_again();
            VNA_REQUIRE(!rejected.has_value());
            VNA_REQUIRE(
                rejected.error().code ==
                instrument::AOnlySubmitErrc::BoardSessionIsolated);
            VNA_REQUIRE(
                rejected.error().retry ==
                acquisition::AcquisitionRetryClass::AfterRecovery);
            VNA_REQUIRE(
                rejected.error().safety ==
                acquisition::AcquisitionSafetyImpact::NoRunAccepted);
            VNA_REQUIRE(rejected.error().board_session == isolated_session);
            const auto after_rejection = harness.control().observations();
            VNA_REQUIRE(after_rejection.accepted_run_calls == 1U);
            VNA_REQUIRE(
                after_rejection.rejected_isolated_execution_reservations ==
                1U);
            const auto event_after_rejection = harness.store().latest_event();
            VNA_REQUIRE(event_after_rejection.has_value());
            VNA_REQUIRE(event_after_rejection->id == previous_event.id);
            VNA_REQUIRE(
                event_after_rejection->revision == previous_event.revision);
        }

        {
            IsolationHarness recovered{provider};
            VNA_REQUIRE(recovered.session_id() != isolated_session);
            VNA_REQUIRE(
                recovered.control().session_state() ==
                board::MockSessionState::Healthy);
            store::OperationId recovered_operation{};
            VNA_REQUIRE(
                recovered.run(healthy_scenario(), recovered_operation));
            VNA_REQUIRE(
                recovered.store()
                    .inspect_completed_sweep(recovered_operation)
                    .has_value());
            VNA_REQUIRE(
                recovered.store().inspect_publications().completed_sweeps ==
                1U);
            const auto observations = recovered.control().observations();
            VNA_REQUIRE(observations.accepted_run_calls == 1U);
            VNA_REQUIRE(observations.run_terminal_callbacks == 1U);
            VNA_REQUIRE(
                observations.consumed_chunk_payloads ==
                observations.run_chunk_callbacks);
            VNA_REQUIRE(observations.isolated_session_transitions == 0U);
        }
    }
}

}  // namespace
