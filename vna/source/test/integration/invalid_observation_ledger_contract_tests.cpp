#include "test_support.h"

#include "adapter/mock/mock_board.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/instrument/instrument_kernel.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>

namespace {

class ObservationLedgerRuntimeClock final
    : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return 0U; }
};

vna::board::MockScenario make_base_scenario() {
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
    scenario.incident_quality = ChunkQuality{
        static_cast<std::uint32_t>(ReceiverQualityFlag::SourceUnleveled)};
    scenario.response_quality = ChunkQuality{
        static_cast<std::uint32_t>(ReceiverQualityFlag::TimebaseUnlocked)};
    return scenario;
}

vna::board::MockScenario make_gap_scenario() {
    using namespace vna::board;

    auto scenario = make_base_scenario();
    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        0U, 2U, 50U, {}};
    scenario.chunk_deliveries[1U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        3U, 3U, 100U, {}};
    scenario.chunk_deliveries[2U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{2U}, ReceiverWave::ResponseB,
        0U, 6U, 150U, {}};
    scenario.chunk_delivery_count = 3U;
    return scenario;
}

vna::board::MockScenario make_conflicting_duplicate_scenario() {
    using namespace vna::board;

    auto scenario = make_base_scenario();
    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        0U, 3U, 50U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::Overload)}};
    scenario.chunk_deliveries[1U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        0U, 3U, 100U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::ReceiverUnlocked)}};
    scenario.chunk_deliveries[2U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{2U}, ReceiverWave::ResponseB,
        0U, 6U, 150U, {}};
    scenario.chunk_delivery_count = 3U;
    return scenario;
}

vna::board::MockScenario make_overlap_scenario() {
    using namespace vna::board;

    auto scenario = make_base_scenario();
    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        0U, 4U, 50U, {}};
    scenario.chunk_deliveries[1U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        3U, 3U, 100U, {}};
    scenario.chunk_deliveries[2U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{2U}, ReceiverWave::ResponseB,
        0U, 6U, 150U, {}};
    scenario.chunk_delivery_count = 3U;
    return scenario;
}

vna::board::MockScenario make_out_of_range_scenario() {
    using namespace vna::board;

    auto scenario = make_base_scenario();
    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        0U, 6U, 50U, {}};
    scenario.chunk_deliveries[1U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{2U}, ReceiverWave::ResponseB,
        6U, 1U, 100U, {}};
    scenario.chunk_delivery_count = 2U;
    return scenario;
}

vna::board::MockScenario make_terminal_before_complete_scenario() {
    using namespace vna::board;

    auto scenario = make_base_scenario();
    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        0U, 6U, 50U, {}};
    scenario.chunk_deliveries[1U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{2U}, ReceiverWave::ResponseB,
        0U, 3U, 100U, {}};
    scenario.chunk_deliveries[2U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{2U}, ReceiverWave::ResponseB,
        3U, 3U, 200U, {}};
    scenario.chunk_delivery_count = 3U;
    scenario.maximum_chunks_before_completed_terminal = 2U;
    return scenario;
}

vna::board::MockScenario make_invalid_payload_scenario() {
    using namespace vna::board;

    auto scenario = make_base_scenario();
    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        0U, 3U, 50U, {}, MockChunkPayloadBehavior::InvalidLease};
    scenario.chunk_delivery_count = 1U;
    return scenario;
}

vna::board::MockScenario make_reordered_complete_scenario() {
    using namespace vna::board;

    auto scenario = make_base_scenario();
    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{2U}, ReceiverWave::ResponseB,
        3U, 3U, 50U, {}};
    scenario.chunk_deliveries[1U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        0U, 3U, 100U, {}};
    scenario.chunk_deliveries[2U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{2U}, ReceiverWave::ResponseB,
        0U, 3U, 150U, {}};
    scenario.chunk_deliveries[3U] = MockChunkDelivery{
        SourceStateId{1U}, ReceiverPathId{1U}, ReceiverWave::IncidentA,
        3U, 3U, 200U, {}};
    scenario.chunk_delivery_count = 4U;
    return scenario;
}

class ObservationLedgerHarness final {
public:
    ObservationLedgerHarness()
        : provider_(vna::board::MockCapabilityProfile{201U}, make_base_scenario()),
          opened_(open(provider_)),
          runtime_(1U, clock_),
          store_(16U),
          acquisition_resources_(1U),
          kernel_(
              runtime_,
              store_,
              opened_.board.execution(),
              acquisition_resources_,
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

    static vna::instrument::AOnlySweepRequest request() noexcept {
        return vna::instrument::AOnlySweepRequest{
            6U,
            1.0e6,
            6.0e6,
            vna::instrument::AOnlyDiagnosticAuthorization::
                issue_for_mock_diagnostics()};
    }

    vna::board::MockBoardProvider provider_;
    vna::board::MockOpenedBoard opened_;
    ObservationLedgerRuntimeClock clock_;
    vna::runtime::OperationRuntime runtime_;
    vna::store::InstrumentStore store_;
    vna::acquisition::AcquisitionAdmissionPool acquisition_resources_;
    vna::instrument::InstrumentKernel kernel_;
};

struct ExpectedObservationFailure final {
    vna::acquisition::AcquisitionFailurePhase phase{};
    vna::acquisition::AcquisitionFailureReason reason{};
    vna::acquisition::NetworkObservationErrc ledger_code{};
    vna::board::ReceiverWave wave{vna::board::ReceiverWave::IncidentA};
    std::uint32_t accepted_points{0U};
    std::uint32_t accepted_chunks{0U};
    std::uint32_t complete_observations{0U};
    std::uint32_t first_missing_point{0U};
    std::uint32_t missing_point_count{0U};
    bool has_offending_chunk{false};
    std::uint32_t offending_point_begin{0U};
    std::uint32_t offending_point_count{0U};
    vna::board::RunTerminalKind terminal_kind{
        vna::board::RunTerminalKind::Completed};
    std::uint32_t terminal_delivered_chunks{0U};
};

TEST(InvalidObservationLedgerContract,
     RejectsEveryIncompleteOrAmbiguousLedgerAndPreservesHistory) {
    using namespace vna;

    ObservationLedgerHarness harness;
    store::OperationId baseline_operation{};
    VNA_REQUIRE(harness.run(make_base_scenario(), baseline_operation));
    const auto baseline = harness.store().inspect_completed_sweep(
        baseline_operation);
    VNA_REQUIRE(baseline.has_value());
    const auto baseline_id = baseline->id();
    const auto baseline_revision = baseline->revision();
    const auto baseline_logical_sweep = baseline->logical_sweep_id();
    const auto baseline_incident_first = baseline->observation(0U).values[0U];
    const auto baseline_incident_last = baseline->observation(0U).values[5U];
    const auto baseline_response_first = baseline->observation(1U).values[0U];
    const auto baseline_response_last = baseline->observation(1U).values[5U];
    const auto baseline_incident_quality =
        baseline->observation(0U).quality_flags[0U];
    const auto baseline_response_quality =
        baseline->observation(1U).quality_flags[5U];
    const auto baseline_run = baseline->board_evidence(0U).run_id;
    const auto baseline_manifest = baseline->board_evidence(0U).manifest.id;

    const auto verify_failure = [&](
        const char* label,
        const board::MockScenario& scenario,
        const ExpectedObservationFailure& expected) {
        SCOPED_TRACE(label);
        store::OperationId operation{};
        VNA_REQUIRE(harness.run(scenario, operation));
        const auto operation_snapshot = harness.store().inspect_operation(operation);
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
        VNA_REQUIRE(!harness.store().inspect_completed_sweep(operation).has_value());

        const auto& failure = event->failure;
        VNA_REQUIRE(failure.phase == expected.phase);
        VNA_REQUIRE(failure.reason == expected.reason);
        VNA_REQUIRE(failure.manifest.valid());
        VNA_REQUIRE(failure.prepared.valid());
        VNA_REQUIRE(failure.run.valid());
        VNA_REQUIRE(failure.generation.valid());
        VNA_REQUIRE(failure.board_session.valid());
        VNA_REQUIRE(failure.capability_revision != 0U);
        VNA_REQUIRE(
            failure.retry == acquisition::AcquisitionRetryClass::AfterRecovery);
        VNA_REQUIRE(
            failure.safety ==
            acquisition::AcquisitionSafetyImpact::RunTerminalObserved);
        VNA_REQUIRE(failure.has_observation_error);
        const auto& ledger = failure.observation_error;
        VNA_REQUIRE(ledger.code == expected.ledger_code);
        VNA_REQUIRE(ledger.manifest == failure.manifest);
        VNA_REQUIRE(ledger.prepared == failure.prepared);
        VNA_REQUIRE(ledger.run == failure.run);
        VNA_REQUIRE(ledger.generation == failure.generation);
        VNA_REQUIRE(ledger.has_observation);
        VNA_REQUIRE(ledger.observation.source_state == board::SourceStateId{1U});
        VNA_REQUIRE(
            ledger.observation.receiver_path ==
            (expected.wave == board::ReceiverWave::IncidentA
                 ? board::ReceiverPathId{1U}
                 : board::ReceiverPathId{2U}));
        VNA_REQUIRE(ledger.observation.wave == expected.wave);
        VNA_REQUIRE(ledger.observation.point_count == 6U);
        VNA_REQUIRE(ledger.coverage.expected_points == 6U);
        VNA_REQUIRE(ledger.coverage.expected_observations == 2U);
        VNA_REQUIRE(
            ledger.coverage.complete_observations ==
            expected.complete_observations);
        VNA_REQUIRE(
            ledger.coverage.accepted_unique_points ==
            expected.accepted_points);
        VNA_REQUIRE(
            ledger.coverage.accepted_chunks == expected.accepted_chunks);
        VNA_REQUIRE(
            ledger.coverage.first_missing_point ==
            expected.first_missing_point);
        VNA_REQUIRE(
            ledger.coverage.missing_point_count ==
            expected.missing_point_count);
        VNA_REQUIRE(
            ledger.has_offending_chunk == expected.has_offending_chunk);
        if (expected.has_offending_chunk) {
            VNA_REQUIRE(
                ledger.offending_chunk.source_state ==
                ledger.observation.source_state);
            VNA_REQUIRE(
                ledger.offending_chunk.receiver_path ==
                ledger.observation.receiver_path);
            VNA_REQUIRE(
                ledger.offending_chunk.wave == ledger.observation.wave);
            VNA_REQUIRE(
                ledger.offending_chunk.point_begin ==
                expected.offending_point_begin);
            VNA_REQUIRE(
                ledger.offending_chunk.point_count ==
                expected.offending_point_count);
        }
        VNA_REQUIRE(
            ledger.has_ingress_disposition ==
            (expected.ledger_code ==
             acquisition::NetworkObservationErrc::IngressRejected));
        if (ledger.has_ingress_disposition) {
            VNA_REQUIRE(
                ledger.ingress_disposition ==
                board::ChunkIngressDisposition::AbortRunProtocolViolation);
        }
        VNA_REQUIRE(ledger.terminal_observed);
        VNA_REQUIRE(ledger.terminal_kind == expected.terminal_kind);
        VNA_REQUIRE(
            ledger.terminal_delivered_chunks ==
            expected.terminal_delivered_chunks);

        const auto baseline_after = harness.store().inspect_completed_sweep(
            baseline_operation);
        VNA_REQUIRE(baseline_after.has_value());
        VNA_REQUIRE(baseline_after->id() == baseline_id);
        VNA_REQUIRE(baseline_after->revision() == baseline_revision);
        VNA_REQUIRE(
            baseline_after->logical_sweep_id() == baseline_logical_sweep);
        VNA_REQUIRE(baseline_after->frequency_hz(0U) == 1.0e6);
        VNA_REQUIRE(baseline_after->frequency_hz(5U) == 6.0e6);
        VNA_REQUIRE(
            baseline_after->observation(0U).values[0U] ==
            baseline_incident_first);
        VNA_REQUIRE(
            baseline_after->observation(0U).values[5U] ==
            baseline_incident_last);
        VNA_REQUIRE(
            baseline_after->observation(1U).values[0U] ==
            baseline_response_first);
        VNA_REQUIRE(
            baseline_after->observation(1U).values[5U] ==
            baseline_response_last);
        VNA_REQUIRE(
            baseline_after->observation(0U).quality_flags[0U] ==
            baseline_incident_quality);
        VNA_REQUIRE(
            baseline_after->observation(1U).quality_flags[5U] ==
            baseline_response_quality);
        VNA_REQUIRE(baseline_after->board_evidence(0U).run_id == baseline_run);
        VNA_REQUIRE(
            baseline_after->board_evidence(0U).manifest.id ==
            baseline_manifest);
        VNA_REQUIRE(harness.store().inspect_publications().completed_sweeps == 1U);
    };

    auto missing_observation = make_base_scenario();
    missing_observation.observation_behavior =
        board::MockObservationBehavior::OmitResponseButComplete;
    verify_failure(
        "missing required observation",
        missing_observation,
        {acquisition::AcquisitionFailurePhase::CandidateSealing,
         acquisition::AcquisitionFailureReason::IncompleteObservationSet,
         acquisition::NetworkObservationErrc::IncompleteCoverage,
         board::ReceiverWave::ResponseB,
         0U, 0U, 1U, 0U, 6U, false, 0U, 0U,
         board::RunTerminalKind::Completed, 1U});
    verify_failure(
        "internal coverage gap",
        make_gap_scenario(),
        {acquisition::AcquisitionFailurePhase::CandidateSealing,
         acquisition::AcquisitionFailureReason::IncompleteObservationSet,
         acquisition::NetworkObservationErrc::IncompleteCoverage,
         board::ReceiverWave::IncidentA,
         5U, 2U, 1U, 2U, 1U, false, 0U, 0U,
         board::RunTerminalKind::Completed, 3U});
    verify_failure(
        "conflicting duplicate",
        make_conflicting_duplicate_scenario(),
        {acquisition::AcquisitionFailurePhase::Run,
         acquisition::AcquisitionFailureReason::BoardContractViolation,
         acquisition::NetworkObservationErrc::ConflictingDuplicate,
         board::ReceiverWave::IncidentA,
         3U, 1U, 0U, 3U, 3U, true, 0U, 3U,
         board::RunTerminalKind::Completed, 3U});
    verify_failure(
        "partial overlap",
        make_overlap_scenario(),
        {acquisition::AcquisitionFailurePhase::Run,
         acquisition::AcquisitionFailureReason::BoardContractViolation,
         acquisition::NetworkObservationErrc::Overlap,
         board::ReceiverWave::IncidentA,
         4U, 1U, 0U, 4U, 2U, true, 3U, 3U,
         board::RunTerminalKind::Completed, 3U});
    verify_failure(
        "out of manifest range",
        make_out_of_range_scenario(),
        {acquisition::AcquisitionFailurePhase::Run,
         acquisition::AcquisitionFailureReason::BoardContractViolation,
         acquisition::NetworkObservationErrc::OutOfRange,
         board::ReceiverWave::ResponseB,
         0U, 0U, 1U, 0U, 6U, true, 6U, 1U,
         board::RunTerminalKind::Completed, 2U});
    verify_failure(
        "completed terminal before planned coverage",
        make_terminal_before_complete_scenario(),
        {acquisition::AcquisitionFailurePhase::CandidateSealing,
         acquisition::AcquisitionFailureReason::IncompleteObservationSet,
         acquisition::NetworkObservationErrc::IncompleteCoverage,
         board::ReceiverWave::ResponseB,
         3U, 1U, 1U, 3U, 3U, false, 0U, 0U,
         board::RunTerminalKind::Completed, 2U});

    auto failed_terminal = make_base_scenario();
    failed_terminal.run_behavior = board::MockRunBehavior::Fail;
    verify_failure(
        "failed terminal",
        failed_terminal,
        {acquisition::AcquisitionFailurePhase::Run,
         acquisition::AcquisitionFailureReason::BoardTerminalFailed,
         acquisition::NetworkObservationErrc::InvalidTerminal,
         board::ReceiverWave::IncidentA,
         0U, 0U, 0U, 0U, 6U, false, 0U, 0U,
         board::RunTerminalKind::Failed, 0U});
    verify_failure(
        "ingress rejects invalid payload",
        make_invalid_payload_scenario(),
        {acquisition::AcquisitionFailurePhase::Run,
         acquisition::AcquisitionFailureReason::BoardContractViolation,
         acquisition::NetworkObservationErrc::IngressRejected,
         board::ReceiverWave::IncidentA,
         0U, 0U, 0U, 0U, 6U, true, 0U, 0U,
         board::RunTerminalKind::Failed, 1U});

    store::OperationId reordered_operation{};
    VNA_REQUIRE(harness.run(
        make_reordered_complete_scenario(), reordered_operation));
    const auto reordered = harness.store().inspect_completed_sweep(
        reordered_operation);
    VNA_REQUIRE(reordered.has_value());
    VNA_REQUIRE(reordered->observation(0U).values[5U] ==
                make_base_scenario().incident_a[5U]);
    VNA_REQUIRE(reordered->observation(1U).values[5U] ==
                make_base_scenario().response_b[5U]);
    VNA_REQUIRE(harness.store().inspect_publications().completed_sweeps == 2U);
}

}  // namespace
