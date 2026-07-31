#include "test_support.h"

#include "adapter/mock/mock_board.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/instrument/instrument_kernel.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

class MultichunkRuntimeClock final : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return 0U; }
};

vna::board::MockScenario make_reordered_scenario(float value_offset) {
    using namespace vna::board;

    MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_behavior = MockRunBehavior::Succeed;
    scenario.run_duration = 350U;
    for (std::size_t index = 0U; index < 6U; ++index) {
        scenario.incident_a[index] = ComplexSample{
            value_offset + static_cast<float>(index),
            0.1F * static_cast<float>(index)};
        scenario.response_b[index] = ComplexSample{
            value_offset + 10.0F + static_cast<float>(index),
            -0.1F * static_cast<float>(index)};
    }

    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U},
        ReceiverPathId{1U},
        ReceiverWave::IncidentA,
        0U,
        3U,
        150U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::SourceUnleveled)}};
    scenario.chunk_deliveries[1U] = MockChunkDelivery{
        SourceStateId{1U},
        ReceiverPathId{2U},
        ReceiverWave::ResponseB,
        3U,
        3U,
        200U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::TimebaseUnlocked)}};
    scenario.chunk_deliveries[2U] = MockChunkDelivery{
        SourceStateId{1U},
        ReceiverPathId{1U},
        ReceiverWave::IncidentA,
        3U,
        3U,
        50U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::Overload)}};
    scenario.chunk_deliveries[3U] = MockChunkDelivery{
        SourceStateId{1U},
        ReceiverPathId{2U},
        ReceiverWave::ResponseB,
        0U,
        3U,
        100U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::ReceiverUnlocked)}};
    scenario.chunk_delivery_count = 4U;
    return scenario;
}

TEST(AOnlyMultichunkContract, ReordersCompleteChunksAndPreservesHistory) {
    using namespace vna;

    auto first_scenario = make_reordered_scenario(1.0F);
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, first_scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    MultichunkRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{2U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};

    const auto request = instrument::AOnlySweepRequest{
        6U,
        1.0e6,
        6.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()};
    const auto first_submission = kernel.submit_a_only(request);
    VNA_REQUIRE(first_submission.has_value());
    const auto first_operation = first_submission.value().operation;
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());

    // 四块在窗口内逐批到达；Mock 的可观察计数证明它们并非拖到 terminal 才突发。
    opened.control->advance(49U);
    VNA_REQUIRE(opened.control->observations().run_chunk_callbacks == 0U);
    opened.control->advance(1U);
    VNA_REQUIRE(opened.control->observations().run_chunk_callbacks == 1U);
    opened.control->advance(50U);
    VNA_REQUIRE(opened.control->observations().run_chunk_callbacks == 2U);
    opened.control->advance(50U);
    VNA_REQUIRE(opened.control->observations().run_chunk_callbacks == 3U);
    opened.control->advance(50U);
    VNA_REQUIRE(opened.control->observations().run_chunk_callbacks == 4U);
    opened.control->advance(149U);
    VNA_REQUIRE(opened.control->observations().run_terminal_callbacks == 0U);
    VNA_REQUIRE(!store.inspect_completed_sweep(first_operation).has_value());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());

    const auto first_snapshot = store.inspect_completed_sweep(first_operation);
    VNA_REQUIRE(first_snapshot.has_value());
    VNA_REQUIRE(first_snapshot->point_count() == 6U);
    VNA_REQUIRE(first_snapshot->frequency_hz(0U) == 1.0e6);
    VNA_REQUIRE(first_snapshot->frequency_hz(5U) == 6.0e6);
    VNA_REQUIRE(first_snapshot->observation_count() == 2U);
    const auto& first_incident = first_snapshot->observation(0U);
    const auto& first_response = first_snapshot->observation(1U);
    VNA_REQUIRE(first_incident.source_state == board::SourceStateId{1U});
    VNA_REQUIRE(first_incident.receiver_path == board::ReceiverPathId{1U});
    VNA_REQUIRE(first_response.receiver_path == board::ReceiverPathId{2U});
    for (std::size_t index = 0U; index < 6U; ++index) {
        VNA_REQUIRE(
            first_incident.values[index] == first_scenario.incident_a[index]);
        VNA_REQUIRE(
            first_response.values[index] == first_scenario.response_b[index]);
    }
    VNA_REQUIRE(
        first_incident.quality_flags[0U] ==
        static_cast<std::uint32_t>(board::ReceiverQualityFlag::SourceUnleveled));
    VNA_REQUIRE(
        first_incident.quality_flags[5U] ==
        static_cast<std::uint32_t>(board::ReceiverQualityFlag::Overload));
    VNA_REQUIRE(
        first_response.quality_flags[0U] ==
        static_cast<std::uint32_t>(board::ReceiverQualityFlag::ReceiverUnlocked));
    VNA_REQUIRE(
        first_response.quality_flags[5U] ==
        static_cast<std::uint32_t>(board::ReceiverQualityFlag::TimebaseUnlocked));

    const auto& first_evidence = first_snapshot->board_evidence(0U);
    VNA_REQUIRE(first_evidence.chunk_count == 4U);
    VNA_REQUIRE(first_evidence.chunks[0U].point_begin == 3U);
    VNA_REQUIRE(first_evidence.chunks[0U].sequence == board::ChunkSequence{1U});
    VNA_REQUIRE(first_evidence.chunks[1U].point_begin == 0U);
    VNA_REQUIRE(first_evidence.chunks[1U].sequence == board::ChunkSequence{2U});
    VNA_REQUIRE(first_evidence.chunks[2U].point_begin == 0U);
    VNA_REQUIRE(first_evidence.chunks[2U].sequence == board::ChunkSequence{3U});
    VNA_REQUIRE(first_evidence.chunks[3U].point_begin == 3U);
    VNA_REQUIRE(first_evidence.chunks[3U].sequence == board::ChunkSequence{4U});
    VNA_REQUIRE(first_evidence.manifest.session_id == board::BoardSessionId{1U});
    VNA_REQUIRE(first_evidence.manifest.capability_revision == 1U);
    VNA_REQUIRE(first_evidence.terminal.run_id == first_evidence.run_id);
    VNA_REQUIRE(first_evidence.terminal.generation == first_evidence.generation);
    VNA_REQUIRE(
        first_evidence.terminal.kind == board::RunTerminalKind::Completed);
    VNA_REQUIRE(first_evidence.terminal.delivered_chunks == 4U);
    VNA_REQUIRE(first_evidence.unique_success_terminal);

    // 第二次扫描复用同一公共入口，但必须形成不同快照并保留第一份完整历史。
    auto second_scenario = make_reordered_scenario(101.0F);
    opened.control->load_scenario(second_scenario);
    const auto second_submission = kernel.submit_a_only(request);
    VNA_REQUIRE(second_submission.has_value());
    const auto second_operation = second_submission.value().operation;
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(350U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());

    const auto second_snapshot = store.inspect_completed_sweep(second_operation);
    const auto first_after_second = store.inspect_completed_sweep(first_operation);
    VNA_REQUIRE(second_snapshot.has_value());
    VNA_REQUIRE(first_after_second.has_value());
    VNA_REQUIRE(second_snapshot->id() != first_snapshot->id());
    VNA_REQUIRE(
        second_snapshot->logical_sweep_id() !=
        first_snapshot->logical_sweep_id());
    VNA_REQUIRE(
        second_snapshot->observation(0U).values[0U] ==
        second_scenario.incident_a[0U]);
    VNA_REQUIRE(first_after_second->id() == first_snapshot->id());
    VNA_REQUIRE(
        first_after_second->observation(0U).values[0U] ==
        first_scenario.incident_a[0U]);
    VNA_REQUIRE(
        first_after_second->observation(0U).quality_flags[0U] ==
        static_cast<std::uint32_t>(
            board::ReceiverQualityFlag::SourceUnleveled));
    VNA_REQUIRE(
        first_after_second->board_evidence(0U).run_id ==
        first_snapshot->board_evidence(0U).run_id);
    VNA_REQUIRE(
        first_after_second->board_evidence(0U).chunks[0U].point_begin == 3U);
    VNA_REQUIRE(
        second_snapshot->board_evidence(0U).run_id !=
        first_snapshot->board_evidence(0U).run_id);
}

TEST(AOnlyMultichunkContract, DerivesProductMaximumChunksFromManifest) {
    using namespace vna;

    board::MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_duration = 350U;
    for (std::size_t index = 0U;
         index < board::kMaximumMockSweepPoints;
         ++index) {
        scenario.incident_a[index] = board::ComplexSample{
            static_cast<float>(index), 1.0F};
        scenario.response_b[index] = board::ComplexSample{
            1000.0F + static_cast<float>(index), -1.0F};
    }

    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    MultichunkRuntimeClock clock;
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
        201U,
        1.0e6,
        201.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());
    const auto operation = submitted.value().operation;
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());

    opened.control->advance(349U);
    VNA_REQUIRE(opened.control->observations().run_chunk_callbacks == 8U);
    VNA_REQUIRE(opened.control->observations().run_terminal_callbacks == 0U);
    VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());

    const auto snapshot = store.inspect_completed_sweep(operation);
    VNA_REQUIRE(snapshot.has_value());
    VNA_REQUIRE(snapshot->point_count() == 201U);
    VNA_REQUIRE(snapshot->observation(0U).values[200U] == scenario.incident_a[200U]);
    VNA_REQUIRE(snapshot->observation(1U).values[200U] == scenario.response_b[200U]);
    const auto& evidence = snapshot->board_evidence(0U);
    VNA_REQUIRE(evidence.chunk_count == 8U);
    VNA_REQUIRE(evidence.chunks[3U].point_begin == 192U);
    VNA_REQUIRE(evidence.chunks[3U].point_count == 9U);
    VNA_REQUIRE(evidence.chunks[4U].wave == board::ReceiverWave::ResponseB);
    VNA_REQUIRE(evidence.chunks[4U].point_begin == 0U);
}

}  // namespace
