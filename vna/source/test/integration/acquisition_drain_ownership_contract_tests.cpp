#include "test_support.h"

#include "adapter/mock/mock_board.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/instrument/instrument_kernel.h"

#include <cstdint>
#include <utility>

namespace {

class DrainRuntimeClock final : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return now_; }

    void set(std::uint64_t now) noexcept { now_ = now; }

private:
    std::uint64_t now_{0U};
};

vna::instrument::AOnlySweepRequest make_request() noexcept {
    return vna::instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        vna::instrument::AOnlyDiagnosticAuthorization::
            issue_for_mock_diagnostics()};
}

TEST(AcquisitionDrainOwnershipContract,
     StalledAcceptedRunTransfersOwnersAndReleasesOnlyAfterDrainTerminal) {
    using namespace vna;

    board::MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_behavior = board::MockRunBehavior::Stall;
    // 350 ms 只属于 Mock Run 的正常耗时模型；Runtime deadline 独立设为 500 tick。
    scenario.run_duration = 350U;
    scenario.incident_a[0U] = board::ComplexSample{1.0F, 0.0F};
    scenario.incident_a[1U] = board::ComplexSample{2.0F, 0.0F};
    scenario.incident_a[2U] = board::ComplexSample{3.0F, 0.0F};
    scenario.response_b[0U] = board::ComplexSample{0.5F, 0.0F};
    scenario.response_b[1U] = board::ComplexSample{1.5F, 0.0F};
    scenario.response_b[2U] = board::ComplexSample{2.5F, 0.0F};

    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    DrainRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{2U};
    acquisition::AcquisitionAdmissionPool resources{1U};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        resources,
        clock,
        instrument::AOnlyKernelProfile{500U, 64U}};

    const auto submitted = kernel.submit_a_only(make_request());
    VNA_REQUIRE(submitted.has_value());
    const auto operation = submitted.value().operation;

    // Prepare 完成后 Run 已 Accepted；即使 Mock 虚拟时间超过 400 ms，Stall
    // 剧本仍不会把 400 ms 偷换成 timeout、abort 或物理 RF 安全证明。
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(400U);
    VNA_REQUIRE(opened.control->observations().accepted_run_calls == 1U);
    VNA_REQUIRE(opened.control->observations().run_terminal_callbacks == 0U);

    // 独立 Runtime 时钟到达 deadline 后，第一次 pump 只形成可靠 handoff；
    // 下一次 pump 才让父失败事实和具名 Drain 通过预留容量同时可见。
    clock.set(500U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_operation(operation)->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(!store.inspect_drain(runtime::DrainId{1U}).has_value());
    VNA_REQUIRE(kernel.run_one());

    const auto parent = store.inspect_operation(operation);
    const auto drain = store.inspect_drain(runtime::DrainId{1U});
    const auto parent_event = store.latest_event();
    VNA_REQUIRE(parent.has_value());
    VNA_REQUIRE(drain.has_value());
    VNA_REQUIRE(parent_event.has_value());
    VNA_REQUIRE(parent->state == store::OperationState::Failed);
    VNA_REQUIRE(drain->operation == operation);
    VNA_REQUIRE(drain->state == store::DrainState::Draining);
    VNA_REQUIRE(drain->revision == parent->revision);
    VNA_REQUIRE(drain->lifecycle_terminal_reserved);
    VNA_REQUIRE(parent_event->has_drain);
    VNA_REQUIRE(parent_event->drain == drain->id);
    VNA_REQUIRE(
        parent_event->failure.reason ==
        acquisition::AcquisitionFailureReason::DeadlineExpired);
    VNA_REQUIRE(drain->ownership.board_run_callback_obligation);
    VNA_REQUIRE(drain->ownership.manifest_owned);
    VNA_REQUIRE(drain->ownership.builder_owned);
    VNA_REQUIRE(drain->ownership.buffer_ingress_owned);
    VNA_REQUIRE(drain->ownership.runtime_completion_registered);
    VNA_REQUIRE(drain->ownership.a_only_completion_owned);
    VNA_REQUIRE(drain->ownership.disabled_preview_owned);
    VNA_REQUIRE(drain->ownership.exact_finalization_consumed);
    VNA_REQUIRE(drain->ownership.run_resources_narrowed);

    VNA_REQUIRE(runtime.inspect().draining == 1U);
    VNA_REQUIRE(resources.inspect().in_use == 1U);
    VNA_REQUIRE(
        opened.control->observations().released_execution_reservations == 0U);
    const auto blocked = kernel.submit_a_only(make_request());
    VNA_REQUIRE(!blocked.has_value());
    VNA_REQUIRE(
        blocked.error().code ==
        instrument::AOnlySubmitErrc::AcquisitionResourcesUnavailable);

    // 控制面只安排一个迟到的底软成功 terminal；不在调用栈内回调。迟到数据
    // 可以被善后 owner 消费，但不得晋升为 A 或把父 Operation 改为 Completed。
    VNA_REQUIRE(opened.control->complete_stalled_run(
        board::MockStalledRunTerminal::Completed));
    VNA_REQUIRE(opened.control->observations().run_terminal_callbacks == 0U);
    opened.control->advance(0U);
    VNA_REQUIRE(opened.control->observations().run_terminal_callbacks == 1U);
    VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());
    VNA_REQUIRE(store.inspect_publications().completed_sweeps == 0U);

    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_drain(drain->id)->state == store::DrainState::Draining);
    VNA_REQUIRE(resources.inspect().in_use == 1U);
    VNA_REQUIRE(kernel.run_one());

    const auto drained = store.inspect_drain(drain->id);
    const auto drain_event = store.latest_drain_event();
    VNA_REQUIRE(drained.has_value());
    VNA_REQUIRE(drain_event.has_value());
    VNA_REQUIRE(drained->state == store::DrainState::Drained);
    VNA_REQUIRE(drain_event->drain == drain->id);
    VNA_REQUIRE(drain_event->state == store::DrainState::Drained);
    VNA_REQUIRE(drain_event->revision == drained->revision);
    VNA_REQUIRE(resources.inspect().in_use == 0U);
    VNA_REQUIRE(resources.inspect().failure_finalizations == 1U);
    VNA_REQUIRE(
        opened.control->observations().released_execution_reservations == 1U);
    VNA_REQUIRE(runtime.inspect().completed == 1U);
    VNA_REQUIRE(runtime.inspect().draining == 0U);
    VNA_REQUIRE(!store.inspect_completed_sweep(operation).has_value());

    // 唯一 Drain terminal 交付并释放整组 owner 后，新扫描才重新获得容量。
    const auto accepted_after_drain = kernel.submit_a_only(make_request());
    VNA_REQUIRE(accepted_after_drain.has_value());
}

TEST(AcquisitionDrainOwnershipContract,
     NormalRunDurationCompletesBeforeIndependentLaterDeadline) {
    using namespace vna;

    board::MockScenario scenario{};
    scenario.prepare_delay = 1U;
    scenario.run_behavior = board::MockRunBehavior::Succeed;
    scenario.run_duration = 350U;
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    DrainRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool resources{1U};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        resources,
        clock,
        instrument::AOnlyKernelProfile{700U, 64U}};

    const auto submitted = kernel.submit_a_only(make_request());
    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(1U);
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(350U);
    clock.set(350U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());

    VNA_REQUIRE(
        store.inspect_operation(submitted.value().operation)->state ==
        store::OperationState::Completed);
    VNA_REQUIRE(store.inspect_publications().completed_sweeps == 1U);
    VNA_REQUIRE(store.inspect().visible_drains == 0U);
}

}  // namespace
