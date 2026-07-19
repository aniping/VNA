#include "test_support.h"

#include "runtime/function/acquisition/a_only_completion_owners.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/acquisition/acquisition_ingress.h"
#include "runtime/function/acquisition/network_observation_builder.h"
#include "runtime/platform/board/acquisition_buffer_pool.h"

#include <array>
#include <type_traits>
#include <utility>

namespace {

static_assert(
    !std::is_copy_constructible_v<vna::acquisition::CandidateCommitLease>);
static_assert(
    !std::is_move_assignable_v<vna::acquisition::AOnlyCompletionOwners>);

TEST(AcquisitionAdmissionContract, ManifestCanOnlyNarrowFrozenEnvelopeOnce) {
    using namespace vna;

    const core::StrongDigest plan_digest{0xA011U};
    const board::CapabilitySnapshot capabilities{
        board::BoardContractVersion{1U, 0U},
        board::BoardSessionId{11U},
        1U,
        2U,
        3U,
        4U,
        core::StrongDigest{0xCA9U},
        201U};
    acquisition::AcquisitionAdmissionPool pool{2U};
    const acquisition::AcquisitionAdmissionPool::Claim claim{
        plan_digest,
        capabilities,
        201U,
        2U,
        1.0e6,
        201.0e6};

    const board::PreparedExecutionManifest expanded{
        board::ManifestId{21U},
        board::PreparedExecutionId{22U},
        capabilities.session_id,
        capabilities.session_epoch,
        capabilities.capability_revision,
        capabilities.topology_epoch,
        capabilities.operational_epoch,
        plan_digest,
        core::StrongDigest{0xAAU},
        201U,
        1.0e6,
        202.0e6,
        std::array<
            board::PreparedObservationSpec,
            board::kMaximumPreparedObservations>{
            board::PreparedObservationSpec{board::ReceiverWave::IncidentA, 201U},
            board::PreparedObservationSpec{board::ReceiverWave::ResponseB, 201U}},
        2U};
    auto narrowed = expanded;
    narrowed.actual_point_count = 101U;
    narrowed.actual_start_hz = 2.0e6;
    narrowed.actual_stop_hz = 200.0e6;
    narrowed.required_observations[0U].point_count = 101U;
    narrowed.required_observations[1U].point_count = 101U;

    {
        auto invalid_reserved = pool.reserve(claim);
        VNA_REQUIRE(invalid_reserved.has_value());
        auto invalid_lease = std::move(invalid_reserved).take_value();
        VNA_REQUIRE(invalid_lease.owns_pre_dispatch_resources());
        VNA_REQUIRE(!invalid_lease.narrow_to(expanded));
        VNA_REQUIRE(!invalid_lease.narrow_to(narrowed));
        VNA_REQUIRE(invalid_lease.finalize_failure());
        VNA_REQUIRE(!invalid_lease.finalize_failure());

        auto valid_reserved = pool.reserve(claim);
        VNA_REQUIRE(valid_reserved.has_value());
        auto valid_lease = std::move(valid_reserved).take_value();
        VNA_REQUIRE(valid_lease.narrow_to(narrowed));
        VNA_REQUIRE(!valid_lease.narrow_to(narrowed));
        VNA_REQUIRE(valid_lease.finalize_failure());
        VNA_REQUIRE(!valid_lease.finalize_failure());
        VNA_REQUIRE(pool.inspect().in_use == 2U);
    }
    VNA_REQUIRE(pool.inspect().in_use == 0U);
    VNA_REQUIRE(pool.inspect().failure_finalizations == 2U);
}

TEST(AcquisitionOwnershipContract, CandidateMovesUntilExplicitAbort) {
    using namespace vna;

    const core::StrongDigest plan_digest{0xB011U};
    const board::PreparedExecutionManifest manifest{
        board::ManifestId{31U},
        board::PreparedExecutionId{32U},
        board::BoardSessionId{33U},
        1U,
        2U,
        3U,
        4U,
        plan_digest,
        core::StrongDigest{0xB012U},
        3U,
        1.0e6,
        3.0e6,
        std::array<
            board::PreparedObservationSpec,
            board::kMaximumPreparedObservations>{
            board::PreparedObservationSpec{board::ReceiverWave::IncidentA, 3U},
            board::PreparedObservationSpec{board::ReceiverWave::ResponseB, 3U}},
        2U};
    const board::BoardRunId run{34U};
    const board::RunGeneration generation{1U};
    acquisition::NetworkObservationBuilder builder{manifest, run, generation};
    std::array<board::ComplexSample, board::kMaximumContractChunkSamples>
        incident{};
    std::array<board::ComplexSample, board::kMaximumContractChunkSamples>
        response{};
    incident[0U] = board::ComplexSample{1.0F, 0.0F};
    response[0U] = board::ComplexSample{0.5F, 0.0F};
    board::AcquisitionBufferPool buffer_pool{2U};
    auto delivery_result = buffer_pool.reserve_delivery(38U, 2U);
    VNA_REQUIRE(delivery_result.has_value());
    auto delivery = std::move(delivery_result).take_value();
    auto incident_payload = delivery.copy_fallback(incident, 3U);
    auto response_payload = delivery.copy_fallback(response, 3U);
    VNA_REQUIRE(incident_payload.has_value());
    VNA_REQUIRE(response_payload.has_value());

    VNA_REQUIRE(
        builder.accept(board::ReceiverObservationChunk{
            manifest.id,
            manifest.prepared_id,
            run,
            generation,
            board::ChunkSequence{1U},
            board::ReceiverWave::IncidentA,
            0U,
            std::move(incident_payload).take_value(),
            board::ChunkQuality{}}) ==
        board::ChunkIngressDisposition::Accepted);
    VNA_REQUIRE(
        builder.accept(board::ReceiverObservationChunk{
            manifest.id,
            manifest.prepared_id,
            run,
            generation,
            board::ChunkSequence{2U},
            board::ReceiverWave::ResponseB,
            0U,
            std::move(response_payload).take_value(),
            board::ChunkQuality{}}) ==
        board::ChunkIngressDisposition::Accepted);
    VNA_REQUIRE(builder.record_terminal(board::BoardRunTerminal{
        run, generation, board::RunTerminalKind::Completed, 2U}));
    auto sealed = builder.seal(
        acquisition::CompletedSweepId{35U},
        acquisition::LogicalSweepId{36U},
        runtime::WorkId{37U},
        plan_digest);
    VNA_REQUIRE(sealed.has_value());
    auto candidate = std::move(sealed).take_value();
    VNA_REQUIRE(candidate.valid());

    auto moved = std::move(candidate);
    VNA_REQUIRE(!candidate.valid());
    VNA_REQUIRE(moved.valid());
    VNA_REQUIRE(moved.abort());
    VNA_REQUIRE(!moved.valid());
    VNA_REQUIRE(!moved.abort());
}

TEST(AcquisitionOwnershipContract, CompletionOwnersRequireMatchingReceipt) {
    using namespace vna;

    const core::StrongDigest plan_digest{0xC011U};
    const board::CapabilitySnapshot capabilities{
        board::BoardContractVersion{1U, 0U},
        board::BoardSessionId{41U},
        1U,
        2U,
        3U,
        4U,
        core::StrongDigest{0xC012U},
        201U};
    const board::PreparedExecutionManifest manifest{
        board::ManifestId{42U},
        board::PreparedExecutionId{43U},
        capabilities.session_id,
        capabilities.session_epoch,
        capabilities.capability_revision,
        capabilities.topology_epoch,
        capabilities.operational_epoch,
        plan_digest,
        core::StrongDigest{0xC013U},
        3U,
        1.0e6,
        3.0e6,
        std::array<
            board::PreparedObservationSpec,
            board::kMaximumPreparedObservations>{
            board::PreparedObservationSpec{board::ReceiverWave::IncidentA, 3U},
            board::PreparedObservationSpec{board::ReceiverWave::ResponseB, 3U}},
        2U};
    acquisition::AcquisitionAdmissionPool pool{1U};
    auto reserved = pool.reserve(acquisition::AcquisitionAdmissionPool::Claim{
        plan_digest, capabilities, 3U, 2U, 1.0e6, 3.0e6});
    VNA_REQUIRE(reserved.has_value());
    auto resources = std::move(reserved).take_value();
    VNA_REQUIRE(resources.narrow_to(manifest));
    acquisition::AOnlyCompletionOwners owners{
        std::move(resources), acquisition::CompletedSweepId{44U}};

    VNA_REQUIRE(!owners.finalize_published(acquisition::CompletedSweepId{45U}));
    VNA_REQUIRE(owners.valid());
    VNA_REQUIRE(pool.inspect().in_use == 1U);
    VNA_REQUIRE(pool.inspect().success_finalizations == 0U);
    VNA_REQUIRE(owners.finalize_published(acquisition::CompletedSweepId{44U}));
    VNA_REQUIRE(!owners.valid());
    VNA_REQUIRE(!owners.finalize_published(acquisition::CompletedSweepId{44U}));
    VNA_REQUIRE(pool.inspect().in_use == 0U);
    VNA_REQUIRE(pool.inspect().success_finalizations == 1U);
}

TEST(AcquisitionOwnershipContract, RejectedChunkIsStillConsumedByReceiver) {
    using namespace vna;

    std::array<board::ComplexSample, board::kMaximumContractChunkSamples>
        samples{};
    samples[0U] = board::ComplexSample{1.0F, -1.0F};
    board::AcquisitionBufferPool buffer_pool{1U};
    auto delivery_result = buffer_pool.reserve_delivery(54U, 1U);
    VNA_REQUIRE(delivery_result.has_value());
    auto delivery = std::move(delivery_result).take_value();
    auto payload = delivery.copy_fallback(samples, 1U);
    VNA_REQUIRE(payload.has_value());
    board::ReceiverObservationChunk rejected{
        board::ManifestId{51U},
        board::PreparedExecutionId{52U},
        board::BoardRunId{53U},
        board::RunGeneration{1U},
        board::ChunkSequence{1U},
        board::ReceiverWave::IncidentA,
        0U,
        std::move(payload).take_value(),
        board::ChunkQuality{}};
    acquisition::AcquisitionIngress invalid_ingress{0U};

    VNA_REQUIRE(
        invalid_ingress.push(std::move(rejected)) ==
        board::ChunkIngressDisposition::AbortRunCapacityBreach);
    // on_chunk() 是无条件所有权边界；即使接收方拒绝，Adapter 也不能再持有 payload。
    VNA_REQUIRE(!rejected.payload.valid());

    const core::StrongDigest plan_digest{0xD011U};
    const board::PreparedExecutionManifest manifest{
        board::ManifestId{55U},
        board::PreparedExecutionId{56U},
        board::BoardSessionId{57U},
        1U,
        2U,
        3U,
        4U,
        plan_digest,
        core::StrongDigest{0xD012U},
        1U,
        1.0e6,
        1.0e6,
        std::array<
            board::PreparedObservationSpec,
            board::kMaximumPreparedObservations>{
            board::PreparedObservationSpec{board::ReceiverWave::IncidentA, 1U}},
        1U};
    acquisition::NetworkObservationBuilder builder{
        manifest, board::BoardRunId{58U}, board::RunGeneration{1U}};
    board::AcquisitionBufferPool builder_pool{1U};
    auto builder_delivery_result = builder_pool.reserve_delivery(59U, 1U);
    VNA_REQUIRE(builder_delivery_result.has_value());
    auto builder_delivery = std::move(builder_delivery_result).take_value();
    auto builder_payload = builder_delivery.copy_fallback(samples, 1U);
    VNA_REQUIRE(builder_payload.has_value());
    board::ReceiverObservationChunk wrong_identity{
        board::ManifestId{60U},
        manifest.prepared_id,
        board::BoardRunId{58U},
        board::RunGeneration{1U},
        board::ChunkSequence{1U},
        board::ReceiverWave::IncidentA,
        0U,
        std::move(builder_payload).take_value(),
        board::ChunkQuality{}};
    VNA_REQUIRE(
        builder.accept(std::move(wrong_identity)) ==
        board::ChunkIngressDisposition::AbortRunProtocolViolation);
    VNA_REQUIRE(!wrong_identity.payload.valid());
}

TEST(AcquisitionBufferContract, ReservedFallbackLeaseMovesWithoutAnotherCopy) {
    using namespace vna;

    board::AcquisitionBufferPool pool{2U};
    auto delivery_result = pool.reserve_delivery(61U, 2U);
    VNA_REQUIRE(delivery_result.has_value());
    auto delivery = std::move(delivery_result).take_value();
    VNA_REQUIRE(delivery.valid());
    VNA_REQUIRE(delivery.remaining_fallback_capacity() == 2U);

    std::array<board::ComplexSample, board::kMaximumContractChunkSamples>
        source{};
    source[0U] = board::ComplexSample{1.0F, -0.5F};
    source[1U] = board::ComplexSample{2.0F, -1.0F};
    auto copied_result = delivery.copy_fallback(source, 2U);
    VNA_REQUIRE(copied_result.has_value());
    auto payload = std::move(copied_result).take_value();
    const auto* const pooled_address = &payload[0U];
    VNA_REQUIRE(pool.inspect().copy_operations == 1U);
    VNA_REQUIRE(pool.inspect().reserved == 1U);
    VNA_REQUIRE(pool.inspect().leased == 1U);

    auto moved_payload = std::move(payload);
    VNA_REQUIRE(!payload.valid());
    VNA_REQUIRE(&moved_payload[0U] == pooled_address);
    board::ReceiverObservationChunk chunk{
        board::ManifestId{62U},
        board::PreparedExecutionId{63U},
        board::BoardRunId{64U},
        board::RunGeneration{1U},
        board::ChunkSequence{1U},
        board::ReceiverWave::IncidentA,
        0U,
        std::move(moved_payload),
        board::ChunkQuality{}};
    acquisition::AcquisitionIngress ingress{1U};
    VNA_REQUIRE(
        ingress.push(std::move(chunk)) ==
        board::ChunkIngressDisposition::Accepted);
    auto received = ingress.pop();
    VNA_REQUIRE(received.has_value());
    VNA_REQUIRE(&received->payload[0U] == pooled_address);
    VNA_REQUIRE(pool.inspect().copy_operations == 1U);

    delivery.retire();
    VNA_REQUIRE(pool.inspect().reserved == 0U);
    VNA_REQUIRE(pool.inspect().leased == 1U);
    received.reset();
    VNA_REQUIRE(pool.inspect().leased == 0U);
}

}  // namespace
