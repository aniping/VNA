#include "test_support.h"

#include "adapter/mock/mock_board.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/instrument/instrument_kernel.h"

#include <cstdint>
#include <utility>

namespace {

class SnapshotRuntimeClock final : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return 0U; }
};

TEST(AOnlySnapshotContract, PublishesManifestDrivenImmutableAAtRunTerminal) {
    using namespace vna;

    board::MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_behavior = board::MockRunBehavior::Succeed;
    scenario.run_delay = 350U;
    scenario.incident_a[0U] = board::ComplexSample{1.0F, 0.25F};
    scenario.incident_a[1U] = board::ComplexSample{2.0F, -0.5F};
    scenario.incident_a[2U] = board::ComplexSample{3.0F, 0.75F};
    scenario.response_b[0U] = board::ComplexSample{0.5F, -0.25F};
    scenario.response_b[1U] = board::ComplexSample{1.5F, 0.5F};
    scenario.response_b[2U] = board::ComplexSample{2.5F, -0.75F};
    scenario.incident_quality.flags = static_cast<std::uint32_t>(
        board::ReceiverQualityFlag::SourceUnleveled);
    scenario.response_quality.flags = static_cast<std::uint32_t>(
        board::ReceiverQualityFlag::ReceiverUnlocked);

    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    SnapshotRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};

    const auto submitted = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());
    const auto operation = submitted.value().operation;
    const auto accepted = store.inspect_operation(operation);
    VNA_REQUIRE(accepted.has_value());
    VNA_REQUIRE(accepted->state == store::OperationState::Accepted);
    VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());

    // 首次 pump 只启动 Prepare；Prepare 到期后的下一次 pump 才接受 Run。
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());

    opened.control->advance(349U);
    VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());
    VNA_REQUIRE(
        store.inspect_operation(operation)->state == store::OperationState::Accepted);

    // Board 回调本身不能写 Store；只有后续 Runtime/L2 pump 才原子发布。
    opened.control->advance(1U);
    VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(acquisition_resources.inspect().success_finalizations == 0U);
    VNA_REQUIRE(kernel.run_one());

    const auto completed_operation = store.inspect_operation(operation);
    const auto completed_sweep = store.inspect_completed_sweep(operation);
    const auto fence = store.inspect_fence(operation);
    const auto status = store.inspect_status();
    const auto event = store.latest_event();
    VNA_REQUIRE(completed_operation.has_value());
    VNA_REQUIRE(completed_sweep.has_value());
    VNA_REQUIRE(fence.has_value());
    VNA_REQUIRE(event.has_value());
    VNA_REQUIRE(completed_operation->state == store::OperationState::Completed);
    VNA_REQUIRE(completed_sweep->operation() == operation);
    VNA_REQUIRE(completed_sweep->revision() == completed_operation->revision);
    VNA_REQUIRE(fence->revision == completed_operation->revision);
    VNA_REQUIRE(status.state == store::OperationState::Completed);
    VNA_REQUIRE(status.operation == operation);
    VNA_REQUIRE(status.revision == completed_operation->revision);
    VNA_REQUIRE(event->revision == completed_operation->revision);
    VNA_REQUIRE(event->state == store::OperationState::Completed);
    VNA_REQUIRE(event->has_completed_sweep);
    VNA_REQUIRE(event->completed_sweep == completed_sweep->id());

    VNA_REQUIRE(completed_sweep->id().valid());
    VNA_REQUIRE(completed_sweep->logical_sweep_id().valid());
    VNA_REQUIRE(completed_sweep->point_count() == 3U);
    VNA_REQUIRE(completed_sweep->frequency_hz(0U) == 1.0e6);
    VNA_REQUIRE(completed_sweep->frequency_hz(1U) == 2.0e6);
    VNA_REQUIRE(completed_sweep->frequency_hz(2U) == 3.0e6);
    VNA_REQUIRE(completed_sweep->observation_count() == 2U);

    const auto& incident = completed_sweep->observation(0U);
    const auto& response = completed_sweep->observation(1U);
    VNA_REQUIRE(incident.wave == board::ReceiverWave::IncidentA);
    VNA_REQUIRE(response.wave == board::ReceiverWave::ResponseB);
    VNA_REQUIRE(incident.point_count == 3U);
    VNA_REQUIRE(response.point_count == 3U);
    VNA_REQUIRE((incident.values[0U] == board::ComplexSample{1.0F, 0.25F}));
    VNA_REQUIRE((incident.values[1U] == board::ComplexSample{2.0F, -0.5F}));
    VNA_REQUIRE((incident.values[2U] == board::ComplexSample{3.0F, 0.75F}));
    VNA_REQUIRE((response.values[0U] == board::ComplexSample{0.5F, -0.25F}));
    VNA_REQUIRE((response.values[1U] == board::ComplexSample{1.5F, 0.5F}));
    VNA_REQUIRE((response.values[2U] == board::ComplexSample{2.5F, -0.75F}));
    VNA_REQUIRE(
        incident.quality_flags[1U] ==
        static_cast<std::uint32_t>(board::ReceiverQualityFlag::SourceUnleveled));
    VNA_REQUIRE(
        response.quality_flags[1U] ==
        static_cast<std::uint32_t>(board::ReceiverQualityFlag::ReceiverUnlocked));

    VNA_REQUIRE(completed_sweep->board_evidence_count() == 1U);
    const auto& evidence = completed_sweep->board_evidence(0U);
    VNA_REQUIRE(evidence.manifest.id.valid());
    VNA_REQUIRE(evidence.manifest.manifest_digest.valid());
    VNA_REQUIRE(evidence.manifest.actual_point_count == 3U);
    VNA_REQUIRE(evidence.run_id.valid());
    VNA_REQUIRE(evidence.generation.valid());
    VNA_REQUIRE(evidence.incident_points == 3U);
    VNA_REQUIRE(evidence.response_points == 3U);
    VNA_REQUIRE(evidence.delivered_chunks == 2U);
    VNA_REQUIRE(evidence.unique_success_terminal);

    const auto publications = store.inspect_publications();
    VNA_REQUIRE(publications.completed_sweeps == 1U);
    VNA_REQUIRE(publications.measurements == 0U);
    VNA_REQUIRE(publications.stages == 0U);
    VNA_REQUIRE(publications.analyses == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().success_finalizations == 1U);
}

TEST(AOnlySnapshotContract, CompletedTerminalCannotPublishMissingRequiredWave) {
    using namespace vna;

    board::MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_behavior = board::MockRunBehavior::Succeed;
    scenario.run_delay = 1U;
    scenario.observation_behavior =
        board::MockObservationBehavior::OmitResponseButComplete;
    scenario.incident_a[0U] = board::ComplexSample{1.0F, 0.0F};
    scenario.incident_a[1U] = board::ComplexSample{2.0F, 0.0F};
    scenario.incident_a[2U] = board::ComplexSample{3.0F, 0.0F};

    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    SnapshotRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};

    const auto submitted = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());
    const auto operation = submitted.value().operation;

    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());

    const auto terminal = store.inspect_operation(operation);
    const auto event = store.latest_event();
    VNA_REQUIRE(terminal.has_value());
    VNA_REQUIRE(event.has_value());
    VNA_REQUIRE(terminal->state == store::OperationState::Failed);
    VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());
    VNA_REQUIRE(store.inspect_publications().completed_sweeps == 0U);
    VNA_REQUIRE(event->has_acquisition_failure);
    VNA_REQUIRE(
        event->failure.phase ==
        acquisition::AcquisitionFailurePhase::CandidateSealing);
    VNA_REQUIRE(
        event->failure.reason ==
        acquisition::AcquisitionFailureReason::IncompleteObservationSet);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().failure_finalizations == 1U);
    VNA_REQUIRE(acquisition_resources.inspect().success_finalizations == 0U);
}

}  // namespace
