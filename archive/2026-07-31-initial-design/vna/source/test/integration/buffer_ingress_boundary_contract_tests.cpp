#include "test_support.h"

#include "adapter/mock/mock_board.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/acquisition/acquisition_ingress.h"
#include "runtime/function/instrument/instrument_kernel.h"
#include "runtime/platform/board/acquisition_buffer_pool.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>

namespace {

class BoundaryRuntimeClock final
    : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return 0U; }
};

vna::instrument::AOnlySweepRequest make_request(
    std::uint32_t point_count) noexcept {
    return vna::instrument::AOnlySweepRequest{
        point_count,
        1.0e6,
        point_count == 1U
            ? 1.0e6
            : static_cast<double>(point_count) * 1.0e6,
        vna::instrument::AOnlyDiagnosticAuthorization::
            issue_for_mock_diagnostics()};
}

vna::board::MockScenario make_healthy_scenario(
    std::uint32_t point_count) noexcept {
    using namespace vna::board;

    MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_duration = 350U;
    scenario.incident_quality = ChunkQuality{static_cast<std::uint32_t>(
        ReceiverQualityFlag::Overload)};
    scenario.response_quality = ChunkQuality{static_cast<std::uint32_t>(
        ReceiverQualityFlag::ReceiverUnlocked)};
    for (std::size_t index = 0U; index < point_count; ++index) {
        scenario.incident_a[index] = ComplexSample{
            0.25F + static_cast<float>(index),
            -0.5F - static_cast<float>(index)};
        scenario.response_b[index] = ComplexSample{
            1000.25F + static_cast<float>(index),
            2000.5F + static_cast<float>(index)};
    }
    return scenario;
}

vna::board::MockScenario make_three_chunk_scenario() noexcept {
    using namespace vna::board;

    auto scenario = make_healthy_scenario(1U);
    scenario.chunk_deliveries[0U] = MockChunkDelivery{
        SourceStateId{1U},
        ReceiverPathId{1U},
        ReceiverWave::IncidentA,
        0U,
        1U,
        50U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::Overload)}};
    scenario.chunk_deliveries[1U] = MockChunkDelivery{
        SourceStateId{1U},
        ReceiverPathId{2U},
        ReceiverWave::ResponseB,
        0U,
        1U,
        50U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::ReceiverUnlocked)}};
    // 第三块故意重复 A；正常 Manifest 只需要前两块。它用来模拟底软在
    // 已通过保守准入后违反交付上界，而不是可预测的普通容量不足。
    scenario.chunk_deliveries[2U] = MockChunkDelivery{
        SourceStateId{1U},
        ReceiverPathId{1U},
        ReceiverWave::IncidentA,
        0U,
        1U,
        50U,
        ChunkQuality{static_cast<std::uint32_t>(
            ReceiverQualityFlag::TimebaseUnlocked)}};
    scenario.chunk_delivery_count = 3U;
    return scenario;
}

class BufferIngressHarness final {
public:
    BufferIngressHarness(
        vna::board::MockScenario scenario,
        vna::instrument::AOnlyKernelProfile profile,
        std::size_t store_capacity = 4U)
        : provider_(vna::board::MockCapabilityProfile{201U}, scenario),
          opened_(open(provider_)),
          runtime_(1U, clock_),
          store_(store_capacity),
          resources_(1U),
          kernel_(
              runtime_,
              store_,
              opened_.board.execution(),
              resources_,
              clock_,
              profile) {}

    vna::instrument::AOnlySubmitResult submit(
        std::uint32_t point_count) noexcept {
        return kernel_.submit_a_only(make_request(point_count));
    }

    void complete_accepted_run() {
        VNA_REQUIRE(kernel_.run_one());
        opened_.control->advance(1U);
        VNA_REQUIRE(kernel_.run_one());
        opened_.control->advance(350U);
        VNA_REQUIRE(kernel_.run_one());
        VNA_REQUIRE(kernel_.run_one());
    }

    void complete_synchronously_rejected_run_cleanup() {
        VNA_REQUIRE(kernel_.run_one());
        opened_.control->advance(1U);
        VNA_REQUIRE(kernel_.run_one());
        opened_.control->advance(1U);
        VNA_REQUIRE(kernel_.run_one());
        VNA_REQUIRE(kernel_.run_one());
    }

    void load_scenario(vna::board::MockScenario scenario) noexcept {
        opened_.control->load_scenario(scenario);
    }

    vna::board::MockBoardControl& control() noexcept {
        return *opened_.control;
    }

    vna::store::InstrumentStore& store() noexcept { return store_; }

    vna::acquisition::AcquisitionAdmissionPool& resources() noexcept {
        return resources_;
    }

private:
    static vna::board::MockOpenedBoard open(
        vna::board::MockBoardProvider& provider) {
        auto opened = provider.open_controlled(vna::board::BoardOpenRequest{
            1U, vna::board::BoardContractVersion{1U, 0U}});
        if (!opened.has_value()) {
            std::terminate();
        }
        return std::move(opened).take_value();
    }

    vna::board::MockBoardProvider provider_;
    vna::board::MockOpenedBoard opened_;
    BoundaryRuntimeClock clock_;
    vna::runtime::OperationRuntime runtime_;
    vna::store::InstrumentStore store_;
    vna::acquisition::AcquisitionAdmissionPool resources_;
    vna::instrument::InstrumentKernel kernel_;
};

TEST(BufferIngressBoundaryContract, PublishesCopiedDataAfterDriverBufferReuse) {
    using namespace vna;

    auto scenario = make_healthy_scenario(201U);
    scenario.driver_buffer_behavior =
        board::MockDriverBufferBehavior::ReuseImmediatelyAfterCallback;
    BufferIngressHarness harness{
        scenario,
        instrument::AOnlyKernelProfile{1000U, 64U}};

    const auto submitted = harness.submit(201U);
    VNA_REQUIRE(submitted.has_value());
    const auto operation = submitted.value().operation;
    harness.complete_accepted_run();

    const auto snapshot = harness.store().inspect_completed_sweep(operation);
    VNA_REQUIRE(snapshot.has_value());
    VNA_REQUIRE(snapshot->point_count() == 201U);
    for (std::size_t index = 0U; index < 201U; ++index) {
        VNA_REQUIRE(
            snapshot->observation(0U).values[index] ==
            scenario.incident_a[index]);
        VNA_REQUIRE(
            snapshot->observation(1U).values[index] ==
            scenario.response_b[index]);
        VNA_REQUIRE(
            snapshot->observation(0U).quality_flags[index] ==
            scenario.incident_quality.flags);
        VNA_REQUIRE(
            snapshot->observation(1U).quality_flags[index] ==
            scenario.response_quality.flags);
    }

    const auto board_facts = harness.control().observations();
    VNA_REQUIRE(board_facts.run_chunk_callbacks == 8U);
    VNA_REQUIRE(board_facts.consumed_chunk_payloads == 8U);
    VNA_REQUIRE(board_facts.reused_driver_buffers == 8U);
    VNA_REQUIRE(board_facts.failed_buffer_copies == 0U);
    VNA_REQUIRE(board_facts.released_execution_reservations == 1U);
    VNA_REQUIRE(harness.resources().inspect().in_use == 0U);
    VNA_REQUIRE(harness.resources().inspect().success_finalizations == 1U);
}

TEST(BufferIngressBoundaryContract, RejectsPredictableCapacityBeforeBoardWork) {
    using namespace vna;

    const auto verify = [](instrument::AOnlyKernelProfile profile) {
        BufferIngressHarness harness{make_healthy_scenario(65U), profile};
        const auto rejected = harness.submit(65U);
        VNA_REQUIRE(!rejected.has_value());
        VNA_REQUIRE(
            rejected.error().code ==
            instrument::AOnlySubmitErrc::AcquisitionResourcesUnavailable);

        const auto store_facts = harness.store().inspect();
        const auto board_facts = harness.control().observations();
        VNA_REQUIRE(store_facts.reserved_lifecycles == 0U);
        VNA_REQUIRE(store_facts.visible_operations == 0U);
        VNA_REQUIRE(store_facts.events == 0U);
        VNA_REQUIRE(board_facts.acquired_execution_reservations == 0U);
        VNA_REQUIRE(board_facts.accepted_prepare_calls == 0U);
        VNA_REQUIRE(board_facts.rejected_prepare_calls == 0U);
        VNA_REQUIRE(harness.resources().inspect().in_use == 0U);

        // 1 点 a/b 只需两个块；同一 Kernel 随后的较小合法请求必须取得首个
        // OperationId，证明前一次拒绝没有留下幽灵 Operation 或 ID owner。
        harness.load_scenario(make_healthy_scenario(1U));
        const auto accepted = harness.submit(1U);
        VNA_REQUIRE(accepted.has_value());
        VNA_REQUIRE(accepted.value().operation == store::OperationId{1U});
        harness.complete_accepted_run();
        VNA_REQUIRE(
            harness.store()
                .inspect_completed_sweep(accepted.value().operation)
                .has_value());
    };

    verify(instrument::AOnlyKernelProfile{
        1000U, 64U, 1000000U, 2U, 8U});
    verify(instrument::AOnlyKernelProfile{
        1000U, 64U, 1000000U, 8U, 2U});

    const auto verify_invalid = [](instrument::AOnlyKernelProfile profile) {
        BufferIngressHarness harness{make_healthy_scenario(1U), profile};
        const auto rejected = harness.submit(1U);
        VNA_REQUIRE(!rejected.has_value());
        VNA_REQUIRE(
            rejected.error().code ==
            instrument::AOnlySubmitErrc::InvalidRequest);
        VNA_REQUIRE(harness.store().inspect().visible_operations == 0U);
        VNA_REQUIRE(
            harness.control()
                .observations()
                .acquired_execution_reservations == 0U);
        VNA_REQUIRE(harness.resources().inspect().in_use == 0U);
    };
    verify_invalid(instrument::AOnlyKernelProfile{
        1000U, 64U, 1000000U, 0U, 8U});
    verify_invalid(instrument::AOnlyKernelProfile{
        1000U,
        64U,
        1000000U,
        8U,
        instrument::AOnlyResourceLimits::kMaximumBuffers + 1U});
}

TEST(BufferIngressBoundaryContract, IngressBreachConsumesLeaseAndFailsWholeRun) {
    using namespace vna;

    BufferIngressHarness harness{
        make_three_chunk_scenario(),
        instrument::AOnlyKernelProfile{
            1000U, 64U, 1000000U, 2U, 3U}};
    const auto submitted = harness.submit(1U);
    VNA_REQUIRE(submitted.has_value());
    const auto operation = submitted.value().operation;
    harness.complete_accepted_run();

    const auto event = harness.store().latest_event();
    VNA_REQUIRE(event.has_value());
    VNA_REQUIRE(event->operation == operation);
    VNA_REQUIRE(event->state == store::OperationState::Failed);
    VNA_REQUIRE(event->has_acquisition_failure);
    VNA_REQUIRE(event->failure.has_observation_error);
    VNA_REQUIRE(
        event->failure.observation_error.code ==
        acquisition::NetworkObservationErrc::IngressRejected);
    VNA_REQUIRE(
        event->failure.observation_error.ingress_disposition ==
        board::ChunkIngressDisposition::AbortRunCapacityBreach);
    VNA_REQUIRE(
        !harness.store().inspect_completed_sweep(operation).has_value());
    VNA_REQUIRE(harness.store().inspect_publications().completed_sweeps == 0U);

    const auto failed_facts = harness.control().observations();
    VNA_REQUIRE(failed_facts.run_chunk_callbacks == 3U);
    VNA_REQUIRE(failed_facts.consumed_chunk_payloads == 3U);
    VNA_REQUIRE(failed_facts.failed_buffer_copies == 0U);
    VNA_REQUIRE(failed_facts.released_execution_reservations == 1U);
    VNA_REQUIRE(harness.resources().inspect().in_use == 0U);
    VNA_REQUIRE(harness.resources().inspect().failure_finalizations == 1U);

    harness.load_scenario(make_healthy_scenario(1U));
    const auto recovered = harness.submit(1U);
    VNA_REQUIRE(recovered.has_value());
    harness.complete_accepted_run();
    VNA_REQUIRE(
        harness.store()
            .inspect_completed_sweep(recovered.value().operation)
            .has_value());
    VNA_REQUIRE(
        harness.control().observations().released_execution_reservations == 2U);
    VNA_REQUIRE(harness.resources().inspect().in_use == 0U);
    VNA_REQUIRE(harness.resources().inspect().success_finalizations == 1U);
}

TEST(BufferIngressBoundaryContract, BufferBreachFailsAndReturnsEveryOwner) {
    using namespace vna;

    BufferIngressHarness harness{
        make_three_chunk_scenario(),
        instrument::AOnlyKernelProfile{
            1000U, 64U, 1000000U, 3U, 2U}};
    const auto submitted = harness.submit(1U);
    VNA_REQUIRE(submitted.has_value());
    const auto operation = submitted.value().operation;
    harness.complete_accepted_run();

    const auto event = harness.store().latest_event();
    VNA_REQUIRE(event.has_value());
    VNA_REQUIRE(event->operation == operation);
    VNA_REQUIRE(event->state == store::OperationState::Failed);
    VNA_REQUIRE(event->has_acquisition_failure);
    VNA_REQUIRE(
        event->failure.reason ==
        acquisition::AcquisitionFailureReason::BoardTerminalFailed);
    VNA_REQUIRE(
        !harness.store().inspect_completed_sweep(operation).has_value());
    VNA_REQUIRE(harness.store().inspect_publications().completed_sweeps == 0U);

    const auto failed_facts = harness.control().observations();
    VNA_REQUIRE(failed_facts.run_chunk_callbacks == 2U);
    VNA_REQUIRE(failed_facts.consumed_chunk_payloads == 2U);
    VNA_REQUIRE(failed_facts.failed_buffer_copies == 1U);
    VNA_REQUIRE(failed_facts.released_execution_reservations == 1U);
    VNA_REQUIRE(harness.resources().inspect().in_use == 0U);
    VNA_REQUIRE(harness.resources().inspect().failure_finalizations == 1U);

    harness.load_scenario(make_healthy_scenario(1U));
    const auto recovered = harness.submit(1U);
    VNA_REQUIRE(recovered.has_value());
    harness.complete_accepted_run();
    VNA_REQUIRE(
        harness.store()
            .inspect_completed_sweep(recovered.value().operation)
            .has_value());
    VNA_REQUIRE(
        harness.control().observations().released_execution_reservations == 2U);
    VNA_REQUIRE(harness.resources().inspect().in_use == 0U);
    VNA_REQUIRE(harness.resources().inspect().success_finalizations == 1U);
}

TEST(BufferIngressBoundaryContract, RunRejectionCleanupReturnsEveryCapacity) {
    using namespace vna;

    auto rejected_scenario = make_healthy_scenario(1U);
    rejected_scenario.run_behavior = board::MockRunBehavior::Reject;
    BufferIngressHarness harness{
        rejected_scenario,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const auto submitted = harness.submit(1U);
    VNA_REQUIRE(submitted.has_value());
    const auto operation = submitted.value().operation;
    harness.complete_synchronously_rejected_run_cleanup();

    const auto event = harness.store().latest_event();
    VNA_REQUIRE(event.has_value());
    VNA_REQUIRE(event->operation == operation);
    VNA_REQUIRE(event->state == store::OperationState::Failed);
    VNA_REQUIRE(event->has_acquisition_failure);
    VNA_REQUIRE(
        event->failure.reason ==
        acquisition::AcquisitionFailureReason::BoardRejected);
    VNA_REQUIRE(!harness.store().inspect_completed_sweep(operation).has_value());

    const auto rejected_facts = harness.control().observations();
    VNA_REQUIRE(rejected_facts.rejected_run_calls == 1U);
    VNA_REQUIRE(rejected_facts.run_chunk_callbacks == 0U);
    VNA_REQUIRE(rejected_facts.run_terminal_callbacks == 0U);
    VNA_REQUIRE(rejected_facts.accepted_discard_calls == 1U);
    VNA_REQUIRE(rejected_facts.discard_terminal_callbacks == 1U);
    VNA_REQUIRE(rejected_facts.released_execution_reservations == 1U);
    VNA_REQUIRE(harness.resources().inspect().in_use == 0U);
    VNA_REQUIRE(harness.resources().inspect().failure_finalizations == 1U);

    // 同一 Kernel 随后必须能再次原子预留完整 8 个 Buffer credit、Board
    // registration 和上层 owner；任一资源只释放一部分都会让本提交失败。
    harness.load_scenario(make_healthy_scenario(1U));
    const auto recovered = harness.submit(1U);
    VNA_REQUIRE(recovered.has_value());
    harness.complete_accepted_run();
    VNA_REQUIRE(
        harness.store()
            .inspect_completed_sweep(recovered.value().operation)
            .has_value());
    VNA_REQUIRE(
        harness.control().observations().released_execution_reservations == 2U);
    VNA_REQUIRE(harness.resources().inspect().in_use == 0U);
    VNA_REQUIRE(harness.resources().inspect().success_finalizations == 1U);
}

TEST(BufferIngressBoundaryContract, FixedLimitsArePublicAndBounded) {
    using namespace vna;

    static_assert(
        instrument::AOnlyResourceLimits::kMaximumOperations ==
        instrument::InstrumentKernel::kMaximumAOnlyOperations);
    static_assert(
        instrument::AOnlyResourceLimits::kRequiredObservations == 2U);
    static_assert(
        instrument::AOnlyResourceLimits::kMaximumPoints ==
        acquisition::kMaximumCompletedSweepPoints);
    static_assert(
        instrument::AOnlyResourceLimits::kMaximumChunks ==
        2U * board::kMaximumChunksPerObservation);
    static_assert(
        instrument::AOnlyResourceLimits::kMaximumEvents ==
        store::InstrumentStore::kMaximumEvents);
    static_assert(
        instrument::AOnlyResourceLimits::kMaximumIngressChunks <=
        acquisition::kMaximumAcquisitionIngressChunks);
    static_assert(
        instrument::AOnlyResourceLimits::kMaximumBuffers <=
        board::AcquisitionBufferPool::kMaximumBuffers);

    board::AcquisitionBufferPool maximum_pool{
        board::AcquisitionBufferPool::kMaximumBuffers};
    board::AcquisitionBufferPool oversized_pool{
        board::AcquisitionBufferPool::kMaximumBuffers + 1U};
    acquisition::AcquisitionIngress maximum_ingress{
        acquisition::kMaximumAcquisitionIngressChunks};
    acquisition::AcquisitionIngress oversized_ingress{
        acquisition::kMaximumAcquisitionIngressChunks + 1U};
    VNA_REQUIRE(
        maximum_pool.inspect().capacity ==
        board::AcquisitionBufferPool::kMaximumBuffers);
    VNA_REQUIRE(oversized_pool.inspect().capacity == 0U);
    VNA_REQUIRE(maximum_ingress.valid());
    VNA_REQUIRE(!oversized_ingress.valid());
}

}  // namespace
