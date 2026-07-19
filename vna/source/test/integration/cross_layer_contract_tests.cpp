#include "test_support.h"

#include "adapter/mock/mock_board.h"
#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/instrument/instrument_kernel.h"
#include "runtime/function/instrument/sweep_admission.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr vna::runtime::ExecutionLimits kExecutionLimits{1000U, 64U};

class ManualRuntimeClock final : public vna::runtime::RuntimeMonotonicClock {
public:
    std::uint64_t now_tick() const noexcept override { return 0U; }
};

class CompletingSweepWork final : public vna::runtime::ImmediateRuntimeWork {
public:
    vna::runtime::RuntimeTerminal execute() noexcept override {
        ++executions;
        return vna::runtime::RuntimeTerminal{
            vna::runtime::RuntimeTerminalKind::Completed};
    }

    std::uint32_t executions{0U};
};

class DeferredSweepWork final : public vna::runtime::RuntimeWork {
public:
    vna::runtime::RuntimeWorkStep start(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        ++starts;
        return vna::runtime::RuntimeWorkStep::running();
    }

    vna::runtime::RuntimeWorkStep resume(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        ++resumes;
        return complete
            ? vna::runtime::RuntimeWorkStep::completed()
            : vna::runtime::RuntimeWorkStep::running();
    }

    bool complete{false};
    std::uint32_t starts{0U};
    std::uint32_t resumes{0U};
};

class DrainingSweepWork final : public vna::runtime::RuntimeWork {
public:
    vna::runtime::RuntimeWorkStep start(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        return vna::runtime::RuntimeWorkStep::running();
    }

    vna::runtime::RuntimeWorkStep resume(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        return vna::runtime::RuntimeWorkStep::draining(
            vna::runtime::DrainId{191U});
    }

    vna::runtime::RuntimeDrainStep resume_drain(
        vna::runtime::ExecutionContext& context) noexcept override {
        (void)context;
        return vna::runtime::RuntimeDrainStep::running();
    }
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

class ContractBreakingBoard final : public vna::board::BoardExecutionPort {
public:
    enum class Fault {
        PrepareAcceptedIdentity,
        RunAcceptedIdentity,
        PrepareDraining,
        ChunkBeforeRunAccepted
    };

    explicit ContractBreakingBoard(Fault fault) noexcept : fault_(fault) {}

    vna::board::CapabilitySnapshot capabilities() const noexcept override {
        return capabilities_;
    }

    std::uint64_t monotonic_tick() const noexcept override { return 0U; }

    vna::core::Result<
        vna::board::BoardExecutionReservation,
        vna::board::BoardError>
    reserve_execution() noexcept override {
        if (active_reservation_.valid()) {
            return vna::core::Result<
                vna::board::BoardExecutionReservation,
                vna::board::BoardError>::failure(
                vna::board::BoardError{vna::board::BoardErrc::ResourceExhausted});
        }
        active_reservation_ = vna::board::BoardExecutionReservationId{1U};
        return vna::core::Result<
            vna::board::BoardExecutionReservation,
            vna::board::BoardError>::success(
            issue_execution_reservation(active_reservation_));
    }

    vna::board::PrepareSubmission begin_prepare(
        const vna::board::BoardExecutionReservation& reservation,
        vna::board::PrepareCallId call,
        vna::board::SweepIntent intent,
        vna::board::PrepareAuthorization&& authorization,
        vna::board::PrepareSinkRegistration&& sink) noexcept override {
        if (reservation.id() != active_reservation_) {
            return vna::board::PrepareRejected{
                vna::board::BoardError{vna::board::BoardErrc::ContractViolation},
                vna::board::ReclaimedPrepareInputs{
                    std::move(intent),
                    std::move(authorization),
                    std::move(sink)}};
        }
        prepare_call_ = call;
        prepare_intent_ = intent;
        prepare_authorization_.emplace(std::move(authorization));
        prepare_sink_.emplace(std::move(sink));
        const auto accepted_call = fault_ == Fault::PrepareAcceptedIdentity
            ? vna::board::PrepareCallId{call.value() + 100U}
            : call;
        return vna::board::PrepareAccepted{accepted_call};
    }

    vna::board::RunSubmission begin_run(
        const vna::board::BoardExecutionReservation& reservation,
        vna::board::BoardRunId run,
        vna::board::RunGeneration generation,
        vna::board::PreparedStartToken&& prepared,
        vna::board::StartAuthorization&& authorization,
        vna::board::RunDeliveryGrant&& delivery,
        vna::board::BoardRunSinkRegistration&& sink) noexcept override {
        if (reservation.id() != active_reservation_) {
            return vna::board::RunRejected{
                vna::board::BoardError{vna::board::BoardErrc::ContractViolation},
                vna::board::ReclaimedRunInputs{
                    std::move(prepared),
                    std::move(authorization),
                    std::move(delivery),
                    std::move(sink)}};
        }
        run_ = run;
        generation_ = generation;
        if (fault_ == Fault::ChunkBeforeRunAccepted) {
            std::array<
                vna::board::ComplexSample,
                vna::board::kMaximumContractChunkSamples> samples{};
            samples[0U] = vna::board::ComplexSample{1.0F, 0.0F};
            auto payload = delivery.copy_fallback(samples, 1U);
            if (!payload.has_value()) {
                return vna::board::RunRejected{
                    vna::board::BoardError{
                        vna::board::BoardErrc::ResourceExhausted},
                    vna::board::ReclaimedRunInputs{
                        std::move(prepared),
                        std::move(authorization),
                        std::move(delivery),
                        std::move(sink)}};
            }
            vna::board::ReceiverObservationChunk chunk{
                vna::board::ManifestId{81U},
                vna::board::PreparedExecutionId{81U},
                run,
                generation,
                vna::board::ChunkSequence{1U},
                vna::board::ReceiverWave::IncidentA,
                0U,
                std::move(payload).take_value(),
                vna::board::ChunkQuality{}};
            early_chunk_disposition_ = sink.sink().on_chunk(std::move(chunk));
            early_chunk_consumed_ = !chunk.payload.valid();
        }
        prepared_.emplace(std::move(prepared));
        start_authorization_.emplace(std::move(authorization));
        delivery_.emplace(std::move(delivery));
        run_sink_.emplace(std::move(sink));
        return vna::board::RunAccepted{
            fault_ == Fault::RunAcceptedIdentity
                ? vna::board::BoardRunId{run.value() + 100U}
                : run,
            generation};
    }

    void complete_prepare() noexcept {
        VNA_REQUIRE(prepare_sink_.has_value());
        const auto prepared_id = vna::board::PreparedExecutionId{81U};
        const auto manifest_digest = vna::core::StrongDigest{0x8181U};
        vna::board::PreparedExecutionManifest manifest{
            vna::board::ManifestId{81U},
            prepared_id,
            capabilities_.session_id,
            capabilities_.session_epoch,
            capabilities_.capability_revision,
            capabilities_.topology_epoch,
            capabilities_.operational_epoch,
            prepare_intent_.digest,
            manifest_digest,
            prepare_intent_.point_count,
            prepare_intent_.start_hz,
            prepare_intent_.stop_hz,
            std::array<
                vna::board::PreparedObservationSpec,
                vna::board::kMaximumPreparedObservations>{
                vna::board::PreparedObservationSpec{
                    vna::board::ReceiverWave::IncidentA,
                    prepare_intent_.point_count},
                vna::board::PreparedObservationSpec{
                    vna::board::ReceiverWave::ResponseB,
                    prepare_intent_.point_count}},
            2U};
        auto sink = std::move(*prepare_sink_);
        prepare_sink_.reset();
        vna::board::PrepareTerminal terminal = vna::board::PrepareSucceeded{
            vna::board::PreparedExecution{
                vna::board::PreparedStartToken{
                    capabilities_.session_id, prepared_id, manifest_digest},
                vna::board::PreparedManifestLease{std::move(manifest)}}};
        sink.sink().on_terminal(std::move(terminal));
    }

    void complete_prepare_draining() noexcept {
        VNA_REQUIRE(prepare_sink_.has_value());
        auto sink = std::move(*prepare_sink_);
        prepare_sink_.reset();
        vna::board::PrepareTerminal terminal = vna::board::PrepareDraining{
            vna::board::BoardPrepareDrainOwner::issue_for_adapter()};
        sink.sink().on_terminal(std::move(terminal));
    }

    void complete_prepare_failure() noexcept {
        VNA_REQUIRE(prepare_sink_.has_value());
        auto sink = std::move(*prepare_sink_);
        prepare_sink_.reset();
        vna::board::PrepareTerminal terminal = vna::board::PrepareFailed{
            vna::board::PrepareCleanupEvidence{},
            vna::board::BoardError{vna::board::BoardErrc::ContractViolation}};
        sink.sink().on_terminal(std::move(terminal));
    }

    void complete_run_failure() noexcept {
        VNA_REQUIRE(run_sink_.has_value());
        delivery_->retire();
        auto sink = std::move(*run_sink_);
        run_sink_.reset();
        sink.sink().on_terminal(vna::board::BoardRunTerminal{
            run_, generation_, vna::board::RunTerminalKind::Failed, 0U});
    }

    void emit_spurious_run_terminal() noexcept {
        VNA_REQUIRE(run_sink_.has_value());
        run_sink_->sink().on_terminal(vna::board::BoardRunTerminal{
            vna::board::BoardRunId{run_.value() + 200U},
            generation_,
            vna::board::RunTerminalKind::Failed,
            0U});
    }

    bool execution_reserved() const noexcept {
        return active_reservation_.valid();
    }

    bool early_chunk_consumed() const noexcept { return early_chunk_consumed_; }

    vna::board::ChunkIngressDisposition early_chunk_disposition() const noexcept {
        return early_chunk_disposition_;
    }

private:
    void release_execution_reservation(
        vna::board::BoardExecutionReservationId id) noexcept override {
        if (active_reservation_ == id) {
            active_reservation_ = vna::board::BoardExecutionReservationId{};
        }
    }

    Fault fault_;
    vna::board::CapabilitySnapshot capabilities_{
        vna::board::BoardContractVersion{1U, 0U},
        vna::board::BoardSessionId{71U},
        1U,
        2U,
        3U,
        4U,
        vna::core::StrongDigest{0x7171U},
        201U};
    vna::board::PrepareCallId prepare_call_{};
    vna::board::SweepIntent prepare_intent_{};
    std::optional<vna::board::PrepareAuthorization> prepare_authorization_{};
    std::optional<vna::board::PrepareSinkRegistration> prepare_sink_{};
    vna::board::BoardRunId run_{};
    vna::board::RunGeneration generation_{};
    std::optional<vna::board::PreparedStartToken> prepared_{};
    std::optional<vna::board::StartAuthorization> start_authorization_{};
    std::optional<vna::board::RunDeliveryGrant> delivery_{};
    std::optional<vna::board::BoardRunSinkRegistration> run_sink_{};
    bool early_chunk_consumed_{false};
    vna::board::ChunkIngressDisposition early_chunk_disposition_{
        vna::board::ChunkIngressDisposition::Accepted};
    vna::board::BoardExecutionReservationId active_reservation_{};
};

struct SubmissionTimingSample final {
    bool accepted{false};
    bool board_call_free{false};
    std::uint64_t elapsed_ns{0U};
};

SubmissionTimingSample measure_a_only_submission(std::uint32_t point_count) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    board::MockScenario scenario{};
    scenario.run_behavior = board::MockRunBehavior::Fail;
    scenario.prepare_delay = 0U;
    scenario.run_duration = 350U;
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    if (!opened_result.has_value()) {
        return SubmissionTimingSample{};
    }
    auto opened = std::move(opened_result).take_value();
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const auto request = instrument::AOnlySweepRequest{
        point_count,
        1.0e6,
        point_count == 1U ? 1.0e6 : 201.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()};

    const auto started = std::chrono::steady_clock::now();
    const auto submitted = kernel.submit_a_only(request);
    const auto finished = std::chrono::steady_clock::now();
    const auto observations = opened.control->observations();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        finished - started);
    const SubmissionTimingSample sample{
        submitted.has_value(),
        observations.accepted_prepare_calls == 0U &&
            observations.rejected_prepare_calls == 0U,
        static_cast<std::uint64_t>(elapsed.count())};
    if (submitted.has_value()) {
        (void)kernel.run_one();
        opened.control->advance(0U);
        (void)kernel.run_one();
        opened.control->advance(0U);
        (void)kernel.run_one();
        (void)kernel.run_one();
    }
    return sample;
}

std::uint64_t median(std::vector<std::uint64_t> samples) {
    std::sort(samples.begin(), samples.end());
    const auto middle = samples.size() / 2U;
    return samples.size() % 2U == 0U
        ? samples[middle - 1U] / 2U + samples[middle] / 2U
        : samples[middle];
}

std::uint64_t percentile95(std::vector<std::uint64_t> samples) {
    std::sort(samples.begin(), samples.end());
    const auto rank = (95U * samples.size() + 99U) / 100U;
    return samples[rank - 1U];
}

TEST(CrossLayerContract, AcceptedCommitPrecedesDispatchAndCompletionCommit) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    instrument::SweepAdmissionController controller{runtime, store};
    CompletingSweepWork work;
    runtime::ImmediateRuntimeWorkAdapter work_adapter{work};
    RecordingSweepCompletion completion;

    auto submitted = controller.submit(
        store::OperationId{51U},
        runtime::WorkId{61U},
        kExecutionLimits,
        work_adapter,
        completion);

    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(store.inspect_operation(store::OperationId{51U}).has_value());
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{51U})->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(runtime.inspect().queued == 1U);
    VNA_REQUIRE(work.executions == 0U);
    VNA_REQUIRE(completion.terminals == 0U);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(work.executions == 1U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{51U})->state ==
        store::OperationState::Accepted);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.operation.id == store::OperationId{51U});
    VNA_REQUIRE(completion.operation.state == store::OperationState::Completed);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{51U})->state ==
        store::OperationState::Completed);
}

TEST(CrossLayerContract, FailedInitialCommitReleasesAllOwnersAndNeverDispatches) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    instrument::SweepAdmissionController controller{runtime, store};
    CompletingSweepWork work;
    runtime::ImmediateRuntimeWorkAdapter work_adapter{work};
    RecordingSweepCompletion completion;

    auto submitted = controller.submit(
        store::OperationId{},
        runtime::WorkId{62U},
        kExecutionLimits,
        work_adapter,
        completion);

    VNA_REQUIRE(!submitted.has_value());
    VNA_REQUIRE(
        submitted.error().code ==
        instrument::SweepAdmissionErrc::StoreInitialCommitRejected);
    VNA_REQUIRE(store.inspect().visible_operations == 0U);
    VNA_REQUIRE(store.inspect().reserved_lifecycles == 0U);
    VNA_REQUIRE(runtime.inspect().reserved == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(!controller.run_one());
    VNA_REQUIRE(work.executions == 0U);
    VNA_REQUIRE(completion.terminals == 0U);
}

TEST(CrossLayerContract, AcceptedOperationRemainsVisibleWhileWorkIsRunning) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    instrument::SweepAdmissionController controller{runtime, store};
    DeferredSweepWork work;
    RecordingSweepCompletion completion;

    auto submitted = controller.submit(
        store::OperationId{71U},
        runtime::WorkId{81U},
        kExecutionLimits,
        work,
        completion);
    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(work.starts == 0U);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(work.starts == 1U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().running == 1U);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Accepted);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(work.resumes == 1U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Accepted);

    work.complete = true;
    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(work.resumes == 2U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Accepted);

    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.operation.state == store::OperationState::Completed);
    VNA_REQUIRE(
        store.inspect_operation(store::OperationId{71U})->state ==
        store::OperationState::Completed);
}

TEST(CrossLayerContract, ActiveDrainRejectsReuseOfItsWorkIdBeforeAccepted) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{2U, clock};
    store::InstrumentStore store{2U};
    instrument::SweepAdmissionController controller{runtime, store};
    DrainingSweepWork draining_work;
    RecordingSweepCompletion draining_completion;

    auto first = controller.submit(
        store::OperationId{72U},
        runtime::WorkId{82U},
        kExecutionLimits,
        draining_work,
        draining_completion);
    VNA_REQUIRE(first.has_value());
    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(controller.run_one());
    VNA_REQUIRE(runtime.inspect().draining == 1U);

    CompletingSweepWork duplicate_work;
    runtime::ImmediateRuntimeWorkAdapter duplicate_adapter{duplicate_work};
    RecordingSweepCompletion duplicate_completion;
    auto duplicate = controller.submit(
        store::OperationId{73U},
        runtime::WorkId{82U},
        kExecutionLimits,
        duplicate_adapter,
        duplicate_completion);

    VNA_REQUIRE(!duplicate.has_value());
    VNA_REQUIRE(
        duplicate.error().code == instrument::SweepAdmissionErrc::DuplicateWorkId);
    VNA_REQUIRE(!store.inspect_operation(store::OperationId{73U}).has_value());
    VNA_REQUIRE(duplicate_work.executions == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(runtime.inspect().draining == 1U);
}

TEST(CrossLayerContract, ControllerCannotConsumeAnotherControllersCompletion) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore first_store{1U};
    store::InstrumentStore second_store{1U};
    instrument::SweepAdmissionController first_controller{runtime, first_store};
    instrument::SweepAdmissionController second_controller{runtime, second_store};
    CompletingSweepWork work;
    runtime::ImmediateRuntimeWorkAdapter work_adapter{work};
    RecordingSweepCompletion completion;

    auto submitted = second_controller.submit(
        store::OperationId{91U},
        runtime::WorkId{92U},
        kExecutionLimits,
        work_adapter,
        completion);
    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(second_controller.run_one());
    VNA_REQUIRE(completion.terminals == 0U);

    VNA_REQUIRE(!first_controller.run_one());
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(
        second_store.inspect_operation(store::OperationId{91U})->state ==
        store::OperationState::Accepted);

    VNA_REQUIRE(second_controller.run_one());
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(
        second_store.inspect_operation(store::OperationId{91U})->state ==
        store::OperationState::Completed);
}

TEST(AOnlySweepContract, ExplicitDiagnosticAuthorizationPrecedesAccepted) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    board::MockScenario scenario{};
    scenario.run_behavior = board::MockRunBehavior::Fail;
    scenario.prepare_delay = 0U;
    scenario.run_duration = 350U;
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};

    const instrument::AOnlySweepRequest unauthorized{
        3U, 1.0e6, 3.0e6, instrument::AOnlyDiagnosticAuthorization{}};
    const auto rejected = kernel.submit_a_only(unauthorized);
    VNA_REQUIRE(!rejected.has_value());
    VNA_REQUIRE(
        rejected.error().code ==
        instrument::AOnlySubmitErrc::DiagnosticAuthorizationRequired);
    VNA_REQUIRE(store.inspect().visible_operations == 0U);
    VNA_REQUIRE(store.inspect().events == 0U);
    VNA_REQUIRE(opened.control->observations().accepted_prepare_calls == 0U);

    const instrument::AOnlySweepRequest authorized{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()};
    const auto accepted = kernel.submit_a_only(authorized);

    VNA_REQUIRE(accepted.has_value());
    VNA_REQUIRE(accepted.value().operation.valid());
    const auto operation = store.inspect_operation(accepted.value().operation);
    VNA_REQUIRE(operation.has_value());
    VNA_REQUIRE(operation->state == store::OperationState::Accepted);
    VNA_REQUIRE(operation->work.valid());
    VNA_REQUIRE(operation->plan_digest.valid());
    VNA_REQUIRE(store.inspect().events == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(opened.control->observations().accepted_prepare_calls == 0U);

    // 测试结束前在计时/断言之外闭合已接受工作，避免以析构代替终态。
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(0U);
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(350U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().failure_finalizations == 1U);
}

TEST(AOnlySweepContract, MockRunFailsAtScheduledThreeHundredFiftyMilliseconds) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    board::MockScenario scenario{};
    scenario.prepare_delay = 5U;
    scenario.run_behavior = board::MockRunBehavior::Fail;
    scenario.run_duration = 350U;
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
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
    const auto operation_id = submitted.value().operation;
    VNA_REQUIRE(
        store.inspect_operation(operation_id)->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(store.inspect_publications().completed_sweeps == 0U);

    // 第一次 Runtime pump 才提交 Prepare；submit() 本身没有等待或调用 Board。
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(opened.control->observations().accepted_prepare_calls == 1U);
    opened.control->advance(5U);
    VNA_REQUIRE(
        store.inspect_operation(operation_id)->state ==
        store::OperationState::Accepted);

    // Acquisition 消费 Prepare terminal 并接受 Run；350ms 从此刻起算。
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(opened.control->observations().accepted_run_calls == 1U);
    opened.control->advance(349U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_operation(operation_id)->state ==
        store::OperationState::Accepted);
    VNA_REQUIRE(store.inspect().events == 0U);
    VNA_REQUIRE(store.inspect_publications().completed_sweeps == 0U);

    opened.control->advance(1U);
    VNA_REQUIRE(opened.control->observations().run_terminal_callbacks == 1U);
    VNA_REQUIRE(
        store.inspect_operation(operation_id)->state ==
        store::OperationState::Accepted);

    // 首个 pump 只让 L4 把失败写入 Runtime completion mailbox。
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_operation(operation_id)->state ==
        store::OperationState::Accepted);
    // 后一个 pump 才由 L2 原子提交权威失败事实。
    VNA_REQUIRE(kernel.run_one());

    const auto operation = store.inspect_operation(operation_id);
    const auto fence = store.inspect_fence(operation_id);
    const auto status = store.inspect_status();
    const auto event = store.latest_event();
    VNA_REQUIRE(operation.has_value());
    VNA_REQUIRE(fence.has_value());
    VNA_REQUIRE(event.has_value());
    VNA_REQUIRE(operation->state == store::OperationState::Failed);
    VNA_REQUIRE(fence->state == store::OperationState::Failed);
    VNA_REQUIRE(status.state == store::OperationState::Failed);
    VNA_REQUIRE(event->state == store::OperationState::Failed);
    VNA_REQUIRE(operation->revision == fence->revision);
    VNA_REQUIRE(operation->revision == status.revision);
    VNA_REQUIRE(operation->revision == event->revision);
    VNA_REQUIRE(event->has_acquisition_failure);
    VNA_REQUIRE(
        event->failure.phase == acquisition::AcquisitionFailurePhase::Run);
    VNA_REQUIRE(
        event->failure.reason ==
        acquisition::AcquisitionFailureReason::BoardTerminalFailed);
    VNA_REQUIRE(event->failure.run.valid());
    VNA_REQUIRE(event->failure.generation.valid());
    VNA_REQUIRE(store.inspect_publications().completed_sweeps == 0U);
    VNA_REQUIRE(store.inspect_publications().measurements == 0U);
    VNA_REQUIRE(store.inspect_publications().stages == 0U);
    VNA_REQUIRE(store.inspect_publications().analyses == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().failure_finalizations == 1U);
    VNA_REQUIRE(!kernel.run_one());
}

TEST(AOnlySweepContract, ContinuationExpiryIsRelativeToCurrentBoardTick) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    board::MockScenario scenario{};
    scenario.prepare_delay = 0U;
    scenario.run_behavior = board::MockRunBehavior::Fail;
    scenario.run_duration = 350U;
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    opened.control->advance(500U);
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U, 400U}};

    const auto submitted = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(0U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(opened.control->observations().accepted_run_calls == 1U);
    opened.control->advance(350U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    const auto event = store.latest_event();
    VNA_REQUIRE(event.has_value());
    VNA_REQUIRE(
        event->failure.reason ==
        acquisition::AcquisitionFailureReason::BoardTerminalFailed);
}

TEST(AOnlySweepContract, ResourceAdmissionFailureCreatesNoGhostFactsOrBoardCall) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{0U};
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, board::MockScenario{}};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};

    const auto rejected = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});

    VNA_REQUIRE(!rejected.has_value());
    VNA_REQUIRE(
        rejected.error().code ==
        instrument::AOnlySubmitErrc::AcquisitionResourcesUnavailable);
    VNA_REQUIRE(store.inspect().visible_operations == 0U);
    VNA_REQUIRE(store.inspect().reserved_lifecycles == 0U);
    VNA_REQUIRE(store.inspect().events == 0U);
    VNA_REQUIRE(runtime.inspect().reserved == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(
        opened.control->observations().acquired_execution_reservations == 1U);
    VNA_REQUIRE(
        opened.control->observations().released_execution_reservations == 1U);
    VNA_REQUIRE(opened.control->observations().accepted_prepare_calls == 0U);
    VNA_REQUIRE(opened.control->observations().rejected_prepare_calls == 0U);
}

TEST(AOnlySweepContract, BoardCapacityIsReservedBeforeFirstDispatch) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{2U, clock};
    store::InstrumentStore store{2U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{2U};
    board::MockScenario scenario{};
    scenario.prepare_delay = 0U;
    scenario.run_behavior = board::MockRunBehavior::Fail;
    scenario.run_duration = 350U;
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, scenario};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const instrument::AOnlySweepRequest request{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()};

    const auto first = kernel.submit_a_only(request);
    const auto second = kernel.submit_a_only(request);

    VNA_REQUIRE(first.has_value());
    VNA_REQUIRE(!second.has_value());
    VNA_REQUIRE(
        second.error().code ==
        instrument::AOnlySubmitErrc::AcquisitionResourcesUnavailable);
    VNA_REQUIRE(store.inspect().visible_operations == 1U);
    VNA_REQUIRE(store.inspect().reserved_lifecycles == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 1U);
    VNA_REQUIRE(runtime.inspect().reserved == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(
        opened.control->observations().acquired_execution_reservations == 1U);
    VNA_REQUIRE(
        opened.control->observations().rejected_execution_reservations == 1U);
    VNA_REQUIRE(
        opened.control->observations().released_execution_reservations == 0U);
    VNA_REQUIRE(opened.control->observations().accepted_prepare_calls == 0U);
    VNA_REQUIRE(opened.control->observations().rejected_prepare_calls == 0U);

    // 测试结束前闭合首项已接受工作，证明真实 Board 槽只在 terminal 后归还。
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(0U);
    VNA_REQUIRE(kernel.run_one());
    opened.control->advance(350U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(
        opened.control->observations().released_execution_reservations == 1U);
}

TEST(AOnlySweepContract, WrongPrepareAcceptedIdentityQuarantinesPreparedResources) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    ContractBreakingBoard board{
        ContractBreakingBoard::Fault::PrepareAcceptedIdentity};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        board,
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const auto submitted = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());

    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_operation(submitted.value().operation)->state ==
        store::OperationState::Failed);
    VNA_REQUIRE(runtime.inspect().draining == 1U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);

    board.complete_prepare();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(runtime.inspect().draining == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());
}

TEST(AOnlySweepContract, WrongPrepareAcceptedIdentityQuarantinesDrainOwner) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    ContractBreakingBoard board{
        ContractBreakingBoard::Fault::PrepareAcceptedIdentity};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        board,
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const auto submitted = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());

    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_operation(submitted.value().operation)->state ==
        store::OperationState::Failed);
    VNA_REQUIRE(runtime.inspect().draining == 1U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());

    board.complete_prepare_draining();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(runtime.inspect().draining == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());
}

TEST(AOnlySweepContract, WrongPrepareAcceptedIdentityDrainsAfterCleanupEvidence) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    ContractBreakingBoard board{
        ContractBreakingBoard::Fault::PrepareAcceptedIdentity};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        board,
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const auto submitted = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());

    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(runtime.inspect().draining == 1U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());

    board.complete_prepare_failure();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(runtime.inspect().draining == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(!board.execution_reserved());
}

TEST(AOnlySweepContract, WrongRunAcceptedIdentityKeepsSinkAliveThroughDrain) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    ContractBreakingBoard board{ContractBreakingBoard::Fault::RunAcceptedIdentity};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        board,
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const auto submitted = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());

    VNA_REQUIRE(kernel.run_one());
    board.complete_prepare();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_operation(submitted.value().operation)->state ==
        store::OperationState::Failed);
    VNA_REQUIRE(runtime.inspect().draining == 1U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);

    board.emit_spurious_run_terminal();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(runtime.inspect().draining == 1U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());

    board.complete_run_failure();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(runtime.inspect().draining == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(!board.execution_reserved());
}

TEST(AOnlySweepContract, EarlyRunChunkIsRejectedAndConsumedAtBoardBoundary) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{1U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    ContractBreakingBoard board{
        ContractBreakingBoard::Fault::ChunkBeforeRunAccepted};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        board,
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const auto submitted = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});
    VNA_REQUIRE(submitted.has_value());

    VNA_REQUIRE(kernel.run_one());
    board.complete_prepare();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        board.early_chunk_disposition() ==
        board::ChunkIngressDisposition::AbortRunProtocolViolation);
    // Adapter 违反时序不改变所有权边界：Engine 拒绝后也必须消费 Pool lease。
    VNA_REQUIRE(board.early_chunk_consumed());

    board.complete_run_failure();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_operation(submitted.value().operation)->state ==
        store::OperationState::Failed);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(!board.execution_reserved());
}

TEST(AOnlySweepContract, PrepareDrainingQuarantinesEveryExecutionOwner) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{2U, clock};
    store::InstrumentStore store{2U};
    acquisition::AcquisitionAdmissionPool acquisition_resources{2U};
    ContractBreakingBoard board{ContractBreakingBoard::Fault::PrepareDraining};
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        board,
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};
    const instrument::AOnlySweepRequest request{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()};
    const auto submitted = kernel.submit_a_only(request);
    VNA_REQUIRE(submitted.has_value());

    VNA_REQUIRE(kernel.run_one());
    board.complete_prepare_draining();
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(
        store.inspect_operation(submitted.value().operation)->state ==
        store::OperationState::Failed);
    VNA_REQUIRE(runtime.inspect().draining == 1U);
    VNA_REQUIRE(kernel.run_one());
    VNA_REQUIRE(kernel.run_one());

    VNA_REQUIRE(runtime.inspect().draining == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());
    VNA_REQUIRE(
        store.latest_event()->failure.reason ==
        acquisition::AcquisitionFailureReason::BoardPrepareDraining);

    const auto rejected = kernel.submit_a_only(request);
    VNA_REQUIRE(!rejected.has_value());
    VNA_REQUIRE(
        rejected.error().code ==
        instrument::AOnlySubmitErrc::AcquisitionResourcesUnavailable);
    VNA_REQUIRE(store.inspect().visible_operations == 1U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 1U);
    VNA_REQUIRE(board.execution_reserved());
}

TEST(AOnlySweepContract, InitialCommitFailureReturnsEveryPreDispatchOwner) {
    using namespace vna;

    ManualRuntimeClock clock;
    runtime::OperationRuntime runtime{1U, clock};
    store::InstrumentStore store{2U};
    auto existing_reservation = store.reserve_lifecycle_terminal();
    VNA_REQUIRE(existing_reservation.has_value());
    auto existing_commit = store.commit_accepted(
        store::OperationId{1U},
        std::move(existing_reservation).take_value());
    VNA_REQUIRE(
        std::holds_alternative<store::AcceptedCommitReceipt>(existing_commit));
    const auto revision_before = store.inspect().revision;
    acquisition::AcquisitionAdmissionPool acquisition_resources{1U};
    board::MockBoardProvider provider{
        board::MockCapabilityProfile{201U}, board::MockScenario{}};
    auto opened_result = provider.open_controlled(
        board::BoardOpenRequest{1U, board::BoardContractVersion{1U, 0U}});
    VNA_REQUIRE(opened_result.has_value());
    auto opened = std::move(opened_result).take_value();
    instrument::InstrumentKernel kernel{
        runtime,
        store,
        opened.board.execution(),
        acquisition_resources,
        clock,
        instrument::AOnlyKernelProfile{1000U, 64U}};

    const auto rejected = kernel.submit_a_only(instrument::AOnlySweepRequest{
        3U,
        1.0e6,
        3.0e6,
        instrument::AOnlyDiagnosticAuthorization::issue_for_mock_diagnostics()});

    VNA_REQUIRE(!rejected.has_value());
    VNA_REQUIRE(
        rejected.error().code ==
        instrument::AOnlySubmitErrc::StoreInitialCommitRejected);
    VNA_REQUIRE(store.inspect().visible_operations == 1U);
    VNA_REQUIRE(store.inspect().reserved_lifecycles == 0U);
    VNA_REQUIRE(store.inspect().events == 0U);
    VNA_REQUIRE(store.inspect().revision == revision_before);
    VNA_REQUIRE(runtime.inspect().reserved == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(acquisition_resources.inspect().in_use == 0U);
    VNA_REQUIRE(
        opened.control->observations().acquired_execution_reservations == 1U);
    VNA_REQUIRE(
        opened.control->observations().released_execution_reservations == 1U);
    VNA_REQUIRE(opened.control->observations().accepted_prepare_calls == 0U);
    VNA_REQUIRE(opened.control->observations().rejected_prepare_calls == 0U);
}

TEST(AOnlySubmissionPerformance, RecordsMinimumAndProductMaximumBaseline) {
    constexpr std::size_t kWarmupRuns = 8U;
    constexpr std::size_t kRecordedRuns = 64U;

    for (std::size_t index = 0U; index < kWarmupRuns; ++index) {
        VNA_REQUIRE(measure_a_only_submission(1U).accepted);
        VNA_REQUIRE(measure_a_only_submission(201U).accepted);
    }

    std::vector<std::uint64_t> minimum_samples;
    std::vector<std::uint64_t> maximum_samples;
    minimum_samples.reserve(kRecordedRuns);
    maximum_samples.reserve(kRecordedRuns);
    for (std::size_t index = 0U; index < kRecordedRuns; ++index) {
        const auto minimum = measure_a_only_submission(1U);
        const auto maximum = measure_a_only_submission(201U);
        VNA_REQUIRE(minimum.accepted);
        VNA_REQUIRE(maximum.accepted);
        VNA_REQUIRE(minimum.board_call_free);
        VNA_REQUIRE(maximum.board_call_free);
        VNA_REQUIRE(minimum.elapsed_ns != std::numeric_limits<std::uint64_t>::max());
        VNA_REQUIRE(maximum.elapsed_ns != std::numeric_limits<std::uint64_t>::max());
        minimum_samples.push_back(minimum.elapsed_ns);
        maximum_samples.push_back(maximum.elapsed_ns);
    }

    RecordProperty("warmup_runs", std::to_string(kWarmupRuns));
    RecordProperty("recorded_runs", std::to_string(kRecordedRuns));
    RecordProperty("minimum_points", "1");
    RecordProperty("minimum_median_ns", std::to_string(median(minimum_samples)));
    RecordProperty("minimum_p95_ns", std::to_string(percentile95(minimum_samples)));
    RecordProperty("maximum_points", "201");
    RecordProperty("maximum_median_ns", std::to_string(median(maximum_samples)));
    RecordProperty("maximum_p95_ns", std::to_string(percentile95(maximum_samples)));
}

}  // namespace
