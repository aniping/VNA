#include "test_support.h"

#include "adapter/mock/mock_board.h"

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

static_assert(!std::is_copy_constructible_v<vna::board::PrepareAuthorization>);
static_assert(!std::is_copy_constructible_v<vna::board::PreparedStartToken>);
static_assert(!std::is_copy_constructible_v<vna::board::StartAuthorization>);
static_assert(!std::is_copy_constructible_v<vna::board::RunDeliveryGrant>);
static_assert(!std::is_copy_constructible_v<vna::board::AcquisitionChunkLease>);
static_assert(std::is_nothrow_move_constructible_v<vna::board::AcquisitionChunkLease>);

class RecordingPrepareSink final : public vna::board::PrepareSink {
public:
    void on_terminal(vna::board::PrepareTerminal&& value) noexcept override {
        ++terminal_count;
        terminal.emplace(std::move(value));
    }

    std::uint32_t terminal_count{0U};
    std::optional<vna::board::PrepareTerminal> terminal{};
};

class RecordingRunSink final : public vna::board::BoardRunSink {
public:
    struct ChunkRecord final {
        vna::board::ReceiverWave wave{vna::board::ReceiverWave::IncidentA};
        std::size_t size{0U};
        vna::board::ComplexSample first{};
        vna::board::ComplexSample last{};
        vna::board::ChunkQuality quality{};
    };

    void on_phase(const vna::board::BoardRunPhaseEvent&) noexcept override {
        ++phase_count;
    }

    vna::board::ChunkIngressDisposition on_chunk(
        vna::board::ReceiverObservationChunk&& chunk) noexcept override {
        if (chunk_count < chunks.size() && chunk.payload.size() > 0U) {
            chunks[chunk_count] = ChunkRecord{
                chunk.wave,
                chunk.payload.size(),
                chunk.payload[0U],
                chunk.payload[chunk.payload.size() - 1U],
                chunk.quality};
        }
        ++chunk_count;
        return vna::board::ChunkIngressDisposition::Accepted;
    }

    void on_terminal(vna::board::BoardRunTerminal&& value) noexcept override {
        ++terminal_count;
        terminal = value;
    }

    std::uint32_t phase_count{0U};
    std::uint32_t chunk_count{0U};
    std::uint32_t terminal_count{0U};
    std::array<ChunkRecord, 4U> chunks{};
    std::optional<vna::board::BoardRunTerminal> terminal{};
};

vna::board::PrepareAuthorization matching_prepare_authorization(
    const vna::board::CapabilitySnapshot& capability,
    vna::core::StrongDigest intent_digest) {
    return vna::board::PrepareAuthorization::issue(
        capability.session_id,
        capability.session_epoch,
        capability.capability_revision,
        capability.topology_epoch,
        capability.operational_epoch,
        intent_digest);
}

TEST(BoardAdapterContract, RejectedPrepareReturnsEveryInputAndNeverCallsBack) {
    using namespace vna::board;

    MockBoardProvider provider{
        MockCapabilityProfile{201U},
        MockScenario{MockPrepareBehavior::Reject, 1U}};
    auto opened_result = provider.open_controlled(
        BoardOpenRequest{1U, BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    VNA_REQUIRE(opened.control != nullptr);

    const auto capability = opened.board.execution().capabilities();
    const SweepIntent intent{3U, 1.0e6, 3.0e6, vna::core::StrongDigest{0x1234U}};
    RecordingPrepareSink sink;
    auto submission = opened.board.execution().begin_prepare(
        PrepareCallId{7U},
        intent,
        matching_prepare_authorization(capability, intent.digest),
        PrepareSinkRegistration{sink});

    VNA_REQUIRE(std::holds_alternative<PrepareRejected>(submission));
    auto rejected = std::get<PrepareRejected>(std::move(submission));
    VNA_REQUIRE(rejected.error.code == BoardErrc::Unsupported);
    VNA_REQUIRE(rejected.reclaimed.intent.point_count == 3U);
    VNA_REQUIRE(rejected.reclaimed.intent.digest == intent.digest);
    VNA_REQUIRE(rejected.reclaimed.authorization.valid());
    VNA_REQUIRE(rejected.reclaimed.sink.valid());
    VNA_REQUIRE(sink.terminal_count == 0U);

    opened.control->advance(100U);
    VNA_REQUIRE(sink.terminal_count == 0U);
    const auto observations = opened.control->observations();
    VNA_REQUIRE(observations.accepted_prepare_calls == 0U);
    VNA_REQUIRE(observations.rejected_prepare_calls == 1U);
    VNA_REQUIRE(observations.prepare_terminal_callbacks == 0U);
}

TEST(BoardAdapterContract, StalePrepareAuthorizationIsRejectedWithoutSideEffects) {
    using namespace vna::board;

    MockBoardProvider provider{
        MockCapabilityProfile{201U},
        MockScenario{MockPrepareBehavior::Succeed, 1U}};
    auto opened_result = provider.open_controlled(
        BoardOpenRequest{1U, BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();

    const auto capability = opened.board.execution().capabilities();
    const SweepIntent intent{3U, 1.0e6, 3.0e6, vna::core::StrongDigest{0x5678U}};
    RecordingPrepareSink sink;
    auto stale_authorization = PrepareAuthorization::issue(
        capability.session_id,
        capability.session_epoch,
        capability.capability_revision + 1U,
        capability.topology_epoch,
        capability.operational_epoch,
        intent.digest);

    auto submission = opened.board.execution().begin_prepare(
        PrepareCallId{8U},
        intent,
        std::move(stale_authorization),
        PrepareSinkRegistration{sink});

    VNA_REQUIRE(std::holds_alternative<PrepareRejected>(submission));
    auto rejected = std::get<PrepareRejected>(std::move(submission));
    VNA_REQUIRE(rejected.error.code == BoardErrc::AuthorizationMismatch);
    VNA_REQUIRE(rejected.reclaimed.authorization.valid());
    VNA_REQUIRE(rejected.reclaimed.sink.valid());
    opened.control->advance(100U);
    VNA_REQUIRE(sink.terminal_count == 0U);
}

TEST(BoardAdapterContract, CapabilityChangePreservesInitialSnapshotAndAdvancesRevision) {
    using namespace vna::board;

    MockBoardProvider provider{
        MockCapabilityProfile{201U},
        MockScenario{MockPrepareBehavior::Succeed, 1U}};
    auto opened_result = provider.open_controlled(
        BoardOpenRequest{1U, BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    VNA_REQUIRE(opened.control != nullptr);

    const auto initial = opened.board.initial_capabilities();
    opened.control->load_profile(MockCapabilityProfile{401U});
    const auto current = opened.board.execution().capabilities();

    VNA_REQUIRE(initial.maximum_points == 201U);
    VNA_REQUIRE(opened.board.initial_capabilities().maximum_points == 201U);
    VNA_REQUIRE(current.maximum_points == 401U);
    VNA_REQUIRE(current.capability_revision == initial.capability_revision + 1U);
    VNA_REQUIRE(current.digest != initial.digest);
}

TEST(BoardAdapterContract, InvalidSweepIntentIsRejectedBeforePrepareIsAccepted) {
    using namespace vna::board;

    MockBoardProvider provider{
        MockCapabilityProfile{3U},
        MockScenario{MockPrepareBehavior::Succeed, 1U}};
    auto opened_result = provider.open_controlled(
        BoardOpenRequest{1U, BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();

    const auto capability = opened.board.execution().capabilities();
    const SweepIntent intent{4U, 3.0e6, 1.0e6, vna::core::StrongDigest{0x7777U}};
    RecordingPrepareSink sink;
    auto submission = opened.board.execution().begin_prepare(
        PrepareCallId{81U},
        intent,
        matching_prepare_authorization(capability, intent.digest),
        PrepareSinkRegistration{sink});

    VNA_REQUIRE(std::holds_alternative<PrepareRejected>(submission));
    auto rejected = std::get<PrepareRejected>(std::move(submission));
    VNA_REQUIRE(rejected.error.code == BoardErrc::InvalidIntent);
    VNA_REQUIRE(rejected.reclaimed.intent.point_count == 4U);
    VNA_REQUIRE(rejected.reclaimed.authorization.valid());
    VNA_REQUIRE(rejected.reclaimed.sink.valid());
    opened.control->advance(100U);
    VNA_REQUIRE(sink.terminal_count == 0U);
}

TEST(BoardAdapterContract, AcceptedPrepareIsNonInlineAndHasOneTerminal) {
    using namespace vna::board;

    MockBoardProvider provider{
        MockCapabilityProfile{201U},
        MockScenario{MockPrepareBehavior::Succeed, 5U}};
    auto opened_result = provider.open_controlled(
        BoardOpenRequest{1U, BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();

    const auto capability = opened.board.execution().capabilities();
    const SweepIntent intent{3U, 1.0e6, 3.0e6, vna::core::StrongDigest{0x9ABCU}};
    RecordingPrepareSink sink;
    auto submission = opened.board.execution().begin_prepare(
        PrepareCallId{9U},
        intent,
        matching_prepare_authorization(capability, intent.digest),
        PrepareSinkRegistration{sink});

    VNA_REQUIRE(std::holds_alternative<PrepareAccepted>(submission));
    VNA_REQUIRE(sink.terminal_count == 0U);
    opened.control->advance(4U);
    VNA_REQUIRE(sink.terminal_count == 0U);
    opened.control->advance(1U);
    VNA_REQUIRE(sink.terminal_count == 1U);
    VNA_REQUIRE(sink.terminal.has_value());
    VNA_REQUIRE(std::holds_alternative<PrepareSucceeded>(*sink.terminal));
    const auto& prepared = std::get<PrepareSucceeded>(*sink.terminal).execution;
    VNA_REQUIRE(prepared.start_token.valid());
    VNA_REQUIRE(prepared.manifest.manifest().intent_digest == intent.digest);
    VNA_REQUIRE(prepared.manifest.manifest().actual_point_count == 3U);

    opened.control->advance(100U);
    VNA_REQUIRE(sink.terminal_count == 1U);
}

TEST(BoardAdapterContract, AcceptedRunEmitsDeterministicWavesAndOneTerminal) {
    using namespace vna::board;

    MockScenario scenario{};
    scenario.prepare_delay = 2U;
    scenario.run_delay = 5U;
    scenario.point_count = 3U;
    scenario.incident_a[0U] = ComplexSample{1.0F, 0.0F};
    scenario.incident_a[1U] = ComplexSample{1.0F, 0.1F};
    scenario.incident_a[2U] = ComplexSample{1.0F, 0.2F};
    scenario.response_b[0U] = ComplexSample{0.1F, -0.1F};
    scenario.response_b[1U] = ComplexSample{0.2F, -0.2F};
    scenario.response_b[2U] = ComplexSample{0.3F, -0.3F};
    scenario.response_quality.flags =
        static_cast<std::uint32_t>(ReceiverQualityFlag::Overload);

    MockBoardProvider provider{MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        BoardOpenRequest{1U, BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();

    const auto capability = opened.board.execution().capabilities();
    const SweepIntent intent{3U, 1.0e6, 3.0e6, vna::core::StrongDigest{0xBCDEU}};
    RecordingPrepareSink prepare_sink;
    auto prepare_submission = opened.board.execution().begin_prepare(
        PrepareCallId{10U},
        intent,
        matching_prepare_authorization(capability, intent.digest),
        PrepareSinkRegistration{prepare_sink});
    VNA_REQUIRE(std::holds_alternative<PrepareAccepted>(prepare_submission));
    opened.control->advance(2U);
    VNA_REQUIRE(prepare_sink.terminal.has_value());
    auto prepare_terminal = std::move(*prepare_sink.terminal);
    auto prepared = std::get<PrepareSucceeded>(std::move(prepare_terminal)).execution;
    const auto& manifest = prepared.manifest.manifest();

    RecordingRunSink run_sink;
    auto start_authorization = StartAuthorization::issue(
        capability.session_id,
        manifest.prepared_id,
        manifest.manifest_digest,
        capability.operational_epoch,
        AcquisitionContinuationAttestation{vna::core::StrongDigest{0xC017U}, 100U});
    auto run_submission = opened.board.execution().begin_run(
        BoardRunId{20U},
        RunGeneration{1U},
        std::move(prepared.start_token),
        std::move(start_authorization),
        RunDeliveryGrant{30U},
        BoardRunSinkRegistration{run_sink});

    VNA_REQUIRE(std::holds_alternative<RunAccepted>(run_submission));
    VNA_REQUIRE(run_sink.phase_count == 0U);
    VNA_REQUIRE(run_sink.chunk_count == 0U);
    VNA_REQUIRE(run_sink.terminal_count == 0U);

    opened.control->advance(4U);
    VNA_REQUIRE(run_sink.terminal_count == 0U);
    opened.control->advance(1U);
    VNA_REQUIRE(run_sink.phase_count == 2U);
    VNA_REQUIRE(run_sink.chunk_count == 2U);
    VNA_REQUIRE(run_sink.chunks[0U].wave == ReceiverWave::IncidentA);
    VNA_REQUIRE(run_sink.chunks[0U].size == 3U);
    VNA_REQUIRE((run_sink.chunks[0U].first == ComplexSample{1.0F, 0.0F}));
    VNA_REQUIRE((run_sink.chunks[0U].last == ComplexSample{1.0F, 0.2F}));
    VNA_REQUIRE(run_sink.chunks[1U].wave == ReceiverWave::ResponseB);
    VNA_REQUIRE((run_sink.chunks[1U].first == ComplexSample{0.1F, -0.1F}));
    VNA_REQUIRE((run_sink.chunks[1U].last == ComplexSample{0.3F, -0.3F}));
    VNA_REQUIRE(
        run_sink.chunks[1U].quality.has(ReceiverQualityFlag::Overload));
    VNA_REQUIRE(run_sink.terminal_count == 1U);
    VNA_REQUIRE(run_sink.terminal->kind == RunTerminalKind::Completed);
    VNA_REQUIRE(run_sink.terminal->delivered_chunks == 2U);

    opened.control->advance(100U);
    VNA_REQUIRE(run_sink.terminal_count == 1U);
}

TEST(BoardAdapterContract, RejectedRunReturnsTokenGrantAndSinkWithoutCallbacks) {
    using namespace vna::board;

    MockScenario scenario{};
    scenario.prepare_delay = 1U;
    MockBoardProvider provider{MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        BoardOpenRequest{1U, BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();

    const auto capability = opened.board.execution().capabilities();
    const SweepIntent intent{3U, 1.0e6, 3.0e6, vna::core::StrongDigest{0xD00DU}};
    RecordingPrepareSink prepare_sink;
    auto prepare_submission = opened.board.execution().begin_prepare(
        PrepareCallId{11U},
        intent,
        matching_prepare_authorization(capability, intent.digest),
        PrepareSinkRegistration{prepare_sink});
    VNA_REQUIRE(std::holds_alternative<PrepareAccepted>(prepare_submission));
    opened.control->advance(1U);
    auto prepare_terminal = std::move(*prepare_sink.terminal);
    auto prepared = std::get<PrepareSucceeded>(std::move(prepare_terminal)).execution;
    const auto& manifest = prepared.manifest.manifest();

    RecordingRunSink run_sink;
    auto invalid_start = StartAuthorization::issue(
        capability.session_id,
        manifest.prepared_id,
        manifest.manifest_digest,
        capability.operational_epoch,
        AcquisitionContinuationAttestation{});
    auto submission = opened.board.execution().begin_run(
        BoardRunId{21U},
        RunGeneration{1U},
        std::move(prepared.start_token),
        std::move(invalid_start),
        RunDeliveryGrant{31U},
        BoardRunSinkRegistration{run_sink});

    VNA_REQUIRE(std::holds_alternative<RunRejected>(submission));
    auto rejected = std::get<RunRejected>(std::move(submission));
    VNA_REQUIRE(rejected.error.code == BoardErrc::AuthorizationMismatch);
    VNA_REQUIRE(rejected.reclaimed.prepared.valid());
    VNA_REQUIRE(rejected.reclaimed.authorization.valid());
    VNA_REQUIRE(rejected.reclaimed.delivery.valid());
    VNA_REQUIRE(rejected.reclaimed.sink.valid());
    VNA_REQUIRE(run_sink.phase_count == 0U);
    VNA_REQUIRE(run_sink.chunk_count == 0U);
    VNA_REQUIRE(run_sink.terminal_count == 0U);

    opened.control->advance(100U);
    VNA_REQUIRE(run_sink.terminal_count == 0U);
    const auto observations = opened.control->observations();
    VNA_REQUIRE(observations.accepted_run_calls == 0U);
    VNA_REQUIRE(observations.rejected_run_calls == 1U);
    VNA_REQUIRE(observations.run_phase_callbacks == 0U);
    VNA_REQUIRE(observations.run_chunk_callbacks == 0U);
    VNA_REQUIRE(observations.run_terminal_callbacks == 0U);
}

}  // namespace
