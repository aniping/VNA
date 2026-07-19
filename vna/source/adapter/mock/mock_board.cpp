#include "adapter/mock/mock_board.h"

#include <new>
#include <optional>
#include <utility>

namespace vna::board {

PrepareAuthorization PrepareAuthorization::issue(
    BoardSessionId session_id,
    std::uint64_t session_epoch,
    std::uint64_t capability_revision,
    std::uint64_t topology_epoch,
    std::uint64_t operational_epoch,
    core::StrongDigest intent_digest) noexcept {
    return PrepareAuthorization(
        session_id,
        session_epoch,
        capability_revision,
        topology_epoch,
        operational_epoch,
        intent_digest);
}

PrepareAuthorization::PrepareAuthorization(
    BoardSessionId session_id,
    std::uint64_t session_epoch,
    std::uint64_t capability_revision,
    std::uint64_t topology_epoch,
    std::uint64_t operational_epoch,
    core::StrongDigest intent_digest) noexcept
    : session_id_(session_id),
      session_epoch_(session_epoch),
      capability_revision_(capability_revision),
      topology_epoch_(topology_epoch),
      operational_epoch_(operational_epoch),
      intent_digest_(intent_digest),
      valid_(true) {}

PrepareAuthorization::PrepareAuthorization(PrepareAuthorization&& other) noexcept
    : session_id_(other.session_id_),
      session_epoch_(other.session_epoch_),
      capability_revision_(other.capability_revision_),
      topology_epoch_(other.topology_epoch_),
      operational_epoch_(other.operational_epoch_),
      intent_digest_(other.intent_digest_),
      valid_(other.valid_) {
    other.invalidate();
}

PrepareAuthorization& PrepareAuthorization::operator=(PrepareAuthorization&& other) noexcept {
    if (this != &other) {
        session_id_ = other.session_id_;
        session_epoch_ = other.session_epoch_;
        capability_revision_ = other.capability_revision_;
        topology_epoch_ = other.topology_epoch_;
        operational_epoch_ = other.operational_epoch_;
        intent_digest_ = other.intent_digest_;
        valid_ = other.valid_;
        other.invalidate();
    }
    return *this;
}

void PrepareAuthorization::invalidate() noexcept {
    session_id_ = BoardSessionId{};
    session_epoch_ = 0U;
    capability_revision_ = 0U;
    topology_epoch_ = 0U;
    operational_epoch_ = 0U;
    intent_digest_ = core::StrongDigest{};
    valid_ = false;
}

StartAuthorization StartAuthorization::issue(
    BoardSessionId session_id,
    PreparedExecutionId prepared_id,
    core::StrongDigest manifest_digest,
    std::uint64_t operational_epoch,
    AcquisitionContinuationAttestation continuation) noexcept {
    return StartAuthorization(
        session_id,
        prepared_id,
        manifest_digest,
        operational_epoch,
        continuation);
}

StartAuthorization::StartAuthorization(
    BoardSessionId session_id,
    PreparedExecutionId prepared_id,
    core::StrongDigest manifest_digest,
    std::uint64_t operational_epoch,
    AcquisitionContinuationAttestation continuation) noexcept
    : session_id_(session_id),
      prepared_id_(prepared_id),
      manifest_digest_(manifest_digest),
      operational_epoch_(operational_epoch),
      continuation_(continuation),
      valid_(true) {}

StartAuthorization::StartAuthorization(StartAuthorization&& other) noexcept
    : session_id_(other.session_id_),
      prepared_id_(other.prepared_id_),
      manifest_digest_(other.manifest_digest_),
      operational_epoch_(other.operational_epoch_),
      continuation_(other.continuation_),
      valid_(other.valid_) {
    other.invalidate();
}

StartAuthorization& StartAuthorization::operator=(StartAuthorization&& other) noexcept {
    if (this != &other) {
        session_id_ = other.session_id_;
        prepared_id_ = other.prepared_id_;
        manifest_digest_ = other.manifest_digest_;
        operational_epoch_ = other.operational_epoch_;
        continuation_ = other.continuation_;
        valid_ = other.valid_;
        other.invalidate();
    }
    return *this;
}

void StartAuthorization::invalidate() noexcept {
    session_id_ = BoardSessionId{};
    prepared_id_ = PreparedExecutionId{};
    manifest_digest_ = core::StrongDigest{};
    operational_epoch_ = 0U;
    continuation_ = AcquisitionContinuationAttestation{};
    valid_ = false;
}

BoardRunSinkRegistration::BoardRunSinkRegistration(
    BoardRunSinkRegistration&& other) noexcept
    : sink_(other.sink_) {
    other.sink_ = nullptr;
}

BoardRunSinkRegistration& BoardRunSinkRegistration::operator=(
    BoardRunSinkRegistration&& other) noexcept {
    if (this != &other) {
        sink_ = other.sink_;
        other.sink_ = nullptr;
    }
    return *this;
}

PreparedStartToken::PreparedStartToken(
    BoardSessionId session_id,
    PreparedExecutionId prepared_id,
    core::StrongDigest manifest_digest) noexcept
    : session_id_(session_id),
      prepared_id_(prepared_id),
      manifest_digest_(manifest_digest),
      valid_(true) {}

PreparedStartToken::PreparedStartToken(PreparedStartToken&& other) noexcept
    : session_id_(other.session_id_),
      prepared_id_(other.prepared_id_),
      manifest_digest_(other.manifest_digest_),
      valid_(other.valid_) {
    other.invalidate();
}

PreparedStartToken& PreparedStartToken::operator=(PreparedStartToken&& other) noexcept {
    if (this != &other) {
        session_id_ = other.session_id_;
        prepared_id_ = other.prepared_id_;
        manifest_digest_ = other.manifest_digest_;
        valid_ = other.valid_;
        other.invalidate();
    }
    return *this;
}

void PreparedStartToken::invalidate() noexcept {
    session_id_ = BoardSessionId{};
    prepared_id_ = PreparedExecutionId{};
    manifest_digest_ = core::StrongDigest{};
    valid_ = false;
}

PrepareSinkRegistration::PrepareSinkRegistration(PrepareSinkRegistration&& other) noexcept
    : sink_(other.sink_) {
    other.sink_ = nullptr;
}

PrepareSinkRegistration& PrepareSinkRegistration::operator=(
    PrepareSinkRegistration&& other) noexcept {
    if (this != &other) {
        sink_ = other.sink_;
        other.sink_ = nullptr;
    }
    return *this;
}

namespace {

constexpr BoardSessionId kMockSessionId{1U};

struct PendingPrepare final {
    // begin_prepare() 接受后，Mock 独占保存全部 move-only 输入直到 due_at。
    PrepareCallId call{};
    SweepIntent intent{};
    PrepareAuthorization authorization;
    PrepareSinkRegistration sink;
    VirtualDuration due_at{0U};
};

struct PendingRun final {
    // 场景在接受 Run 时复制，测试随后修改全局场景不会改变已排队执行。
    BoardRunId run{};
    RunGeneration generation{};
    PreparedStartToken prepared;
    StartAuthorization authorization;
    RunDeliveryGrant delivery;
    BoardRunSinkRegistration sink;
    /// 与 PreparedStartToken 同源的实际清单；Run 输出只能由它决定形状。
    PreparedExecutionManifest manifest{};
    MockScenario scenario{};
    VirtualDuration due_at{0U};
};

enum class MockExecutionReservationPhase {
    None,
    Reserved,
    Preparing,
    Prepared,
    Running,
    Terminal
};

class MockBoardSession final : public BoardSession,
                               public BoardExecutionPort,
                               public MockBoardControl {
public:
    MockBoardSession(MockCapabilityProfile profile, MockScenario scenario) noexcept
        : profile_(profile), scenario_(scenario) {
        rebuild_capabilities();
        initial_capabilities_ = capabilities_;
    }

    BoardExecutionPort& execution() noexcept override { return *this; }
    const CapabilitySnapshot& initial_capabilities() const noexcept override {
        return initial_capabilities_;
    }

    CapabilitySnapshot capabilities() const noexcept override { return capabilities_; }
    std::uint64_t monotonic_tick() const noexcept override { return now_; }

    core::Result<BoardExecutionReservation, BoardError>
    reserve_execution() noexcept override {
        if (active_execution_reservation_.valid() ||
            pending_prepare_.has_value() || pending_run_.has_value()) {
            ++observations_.rejected_execution_reservations;
            return core::Result<BoardExecutionReservation, BoardError>::failure(
                BoardError{BoardErrc::ResourceExhausted});
        }
        active_execution_reservation_ =
            BoardExecutionReservationId{next_execution_reservation_id_++};
        execution_reservation_phase_ = MockExecutionReservationPhase::Reserved;
        active_prepared_id_ = PreparedExecutionId{};
        active_manifest_digest_ = core::StrongDigest{};
        active_manifest_ = PreparedExecutionManifest{};
        ++observations_.acquired_execution_reservations;
        return core::Result<BoardExecutionReservation, BoardError>::success(
            issue_execution_reservation(active_execution_reservation_));
    }

    PrepareSubmission begin_prepare(
        const BoardExecutionReservation& reservation,
        PrepareCallId call,
        SweepIntent intent,
        PrepareAuthorization&& authorization,
        PrepareSinkRegistration&& sink) noexcept override {
        auto reject = [&](BoardErrc code, bool consume) mutable
            -> PrepareSubmission {
            if (consume) {
                execution_reservation_phase_ =
                    MockExecutionReservationPhase::Terminal;
            }
            ++observations_.rejected_prepare_calls;
            return PrepareRejected{
                BoardError{code},
                ReclaimedPrepareInputs{
                    std::move(intent), std::move(authorization), std::move(sink)}};
        };

        // 预留身份与 Reserved phase 共同形成一次性 Prepare call capability。
        if (!owns_execution_reservation(reservation) ||
            reservation.id() != active_execution_reservation_ ||
            execution_reservation_phase_ !=
                MockExecutionReservationPhase::Reserved) {
            return reject(BoardErrc::ContractViolation, false);
        }
        if (!call.valid() || !sink.valid()) {
            return reject(BoardErrc::ContractViolation, true);
        }

        // 所有同步拒绝分支都必须返还 intent、授权和 sink，且绝不安排回调。
        if (scenario_.prepare_behavior == MockPrepareBehavior::Reject) {
            return reject(BoardErrc::Unsupported, true);
        }

        const bool intent_is_valid = intent.point_count > 0U &&
            intent.point_count <= capabilities_.maximum_points &&
            intent.start_hz > 0.0 &&
            (intent.point_count == 1U
                 ? intent.stop_hz == intent.start_hz
                 : intent.stop_hz > intent.start_hz) &&
            intent.digest.valid();
        if (!intent_is_valid) {
            return reject(BoardErrc::InvalidIntent, true);
        }

        const bool authorization_matches = authorization.valid() &&
            authorization.session_id() == capabilities_.session_id &&
            authorization.session_epoch() == capabilities_.session_epoch &&
            authorization.capability_revision() == capabilities_.capability_revision &&
            authorization.topology_epoch() == capabilities_.topology_epoch &&
            authorization.operational_epoch() == capabilities_.operational_epoch &&
            authorization.intent_digest() == intent.digest;
        if (!authorization_matches) {
            return reject(BoardErrc::AuthorizationMismatch, true);
        }
        if (pending_prepare_.has_value()) {
            return reject(BoardErrc::Busy, true);
        }

        // 接受只登记待办；即使 delay 为 0，也要等测试显式 advance() 才回调。
        ++observations_.accepted_prepare_calls;
        execution_reservation_phase_ = MockExecutionReservationPhase::Preparing;
        pending_prepare_.emplace(PendingPrepare{
            call,
            std::move(intent),
            std::move(authorization),
            std::move(sink),
            now_ + scenario_.prepare_delay});
        return PrepareAccepted{call};
    }

    RunSubmission begin_run(
        const BoardExecutionReservation& reservation,
        BoardRunId run,
        RunGeneration generation,
        PreparedStartToken&& prepared,
        StartAuthorization&& authorization,
        RunDeliveryGrant&& delivery,
        BoardRunSinkRegistration&& sink) noexcept override {
        auto reject = [&](BoardErrc code, bool consume) mutable -> RunSubmission {
            if (consume) {
                execution_reservation_phase_ =
                    MockExecutionReservationPhase::Terminal;
            }
            // Run 拒绝与 Prepare 相同：调用者重新取得所有 move-only 输入的所有权。
            ++observations_.rejected_run_calls;
            return RunRejected{
                BoardError{code},
                ReclaimedRunInputs{
                    std::move(prepared),
                    std::move(authorization),
                    std::move(delivery),
                    std::move(sink)}};
        };

        if (!owns_execution_reservation(reservation) ||
            reservation.id() != active_execution_reservation_ ||
            execution_reservation_phase_ !=
                MockExecutionReservationPhase::Prepared) {
            return reject(BoardErrc::ContractViolation, false);
        }

        if (!run.valid() || !generation.valid() || !sink.valid()) {
            return reject(BoardErrc::ContractViolation, true);
        }
        if (scenario_.run_behavior == MockRunBehavior::Reject) {
            return reject(BoardErrc::Unsupported, true);
        }
        if (pending_run_.has_value()) {
            return reject(BoardErrc::Busy, true);
        }
        if (scenario_.run_behavior == MockRunBehavior::Succeed &&
            (active_manifest_.actual_point_count == 0U ||
             active_manifest_.actual_point_count > kMaximumContractChunkSamples ||
             active_manifest_.required_observation_count == 0U ||
             active_manifest_.required_observation_count >
                 active_manifest_.required_observations.size())) {
            return reject(BoardErrc::ResourceExhausted, true);
        }

        const bool authorization_matches = prepared.valid() && authorization.valid() &&
            delivery.valid() && sink.valid() && run.valid() && generation.valid() &&
            prepared.session_id() == capabilities_.session_id &&
            prepared.prepared_id() == active_prepared_id_ &&
            prepared.manifest_digest() == active_manifest_digest_ &&
            authorization.session_id() == capabilities_.session_id &&
            authorization.prepared_id() == prepared.prepared_id() &&
            authorization.manifest_digest() == prepared.manifest_digest() &&
            authorization.operational_epoch() == capabilities_.operational_epoch &&
            authorization.continuation().valid() &&
            authorization.continuation().expires_at > now_;
        if (!authorization_matches) {
            return reject(BoardErrc::AuthorizationMismatch, true);
        }
        if (scenario_.run_behavior == MockRunBehavior::Succeed &&
            delivery.remaining_fallback_capacity() <
                active_manifest_.required_observation_count) {
            return reject(BoardErrc::ResourceExhausted, true);
        }

        // 捕获当前场景，保证一旦接受，输出波形和质量标记就不再被配置修改影响。
        ++observations_.accepted_run_calls;
        execution_reservation_phase_ = MockExecutionReservationPhase::Running;
        pending_run_.emplace(PendingRun{
            run,
            generation,
            std::move(prepared),
            std::move(authorization),
            std::move(delivery),
            std::move(sink),
            active_manifest_,
            scenario_,
            now_ + scenario_.run_delay});
        return RunAccepted{run, generation};
    }

    void load_profile(MockCapabilityProfile profile) noexcept override {
        profile_ = profile;
        ++capabilities_.capability_revision;
        rebuild_capabilities();
    }

    void load_scenario(MockScenario scenario) noexcept override { scenario_ = scenario; }

    void advance(VirtualDuration delta) noexcept override {
        now_ += delta;
        // 虚拟时钟取代 sleep/线程：测试对异步事件发生时刻拥有完全控制权。
        if (pending_prepare_.has_value() && pending_prepare_->due_at <= now_) {
            complete_prepare();
        }

        if (pending_run_.has_value() && pending_run_->due_at <= now_) {
            complete_run();
        }
    }

    MockObservationSnapshot observations() const noexcept override {
        return observations_;
    }

private:
    void release_execution_reservation(
        BoardExecutionReservationId id) noexcept override {
        // generation-bound 身份避免旧租约析构释放后来复用的新执行槽；若调用者
        // 违反契约，在 Accepted terminal 前销毁租约，则宁可保持槽位占用也不允许
        // 新请求复用仍持有 callback sink 的底层容量。
        if (active_execution_reservation_ == id &&
            !pending_prepare_.has_value() && !pending_run_.has_value()) {
            active_execution_reservation_ = BoardExecutionReservationId{};
            execution_reservation_phase_ = MockExecutionReservationPhase::None;
            active_prepared_id_ = PreparedExecutionId{};
            active_manifest_digest_ = core::StrongDigest{};
            active_manifest_ = PreparedExecutionManifest{};
            ++observations_.released_execution_reservations;
        }
    }

    void complete_prepare() noexcept {
        if (!pending_prepare_.has_value()) {
            return;
        }

        // 先从会话摘除待办并进入 Prepared，再回调；sink 可重入提交唯一 Run，
        // 但同一 reservation 不能再次 Prepare。
        auto pending = std::move(*pending_prepare_);
        pending_prepare_.reset();
        execution_reservation_phase_ = MockExecutionReservationPhase::Prepared;
        const auto prepared_id = PreparedExecutionId{next_prepared_id_++};
        const core::StrongDigest manifest_digest{
            pending.intent.digest.value ^ 0xA5A5A5A5A5A5A5A5ULL};
        // Run 必须精确消费同一 reservation 刚刚产出的 Prepared 身份，不能只
        // 接受一对由调用者自行伪造、但彼此自洽的 token/authorization。
        active_prepared_id_ = prepared_id;
        active_manifest_digest_ = manifest_digest;
        PreparedExecutionManifest manifest{
            ManifestId{prepared_id.value()},
            prepared_id,
            capabilities_.session_id,
            capabilities_.session_epoch,
            capabilities_.capability_revision,
            capabilities_.topology_epoch,
            capabilities_.operational_epoch,
            pending.intent.digest,
            manifest_digest,
            pending.intent.point_count,
            pending.intent.start_hz,
            pending.intent.stop_hz,
            std::array<
                PreparedObservationSpec,
                kMaximumPreparedObservations>{
                PreparedObservationSpec{
                    ReceiverWave::IncidentA, pending.intent.point_count},
                PreparedObservationSpec{
                    ReceiverWave::ResponseB, pending.intent.point_count}},
            2U};
        active_manifest_ = manifest;
        // 清单摘要把后续 PreparedStartToken/StartAuthorization 绑定到本次 Prepare。
        PrepareTerminal terminal = PrepareSucceeded{PreparedExecution{
            PreparedStartToken{capabilities_.session_id, prepared_id, manifest_digest},
            PreparedManifestLease{std::move(manifest)}}};
        ++observations_.prepare_terminal_callbacks;
        pending.sink.sink().on_terminal(std::move(terminal));
    }

    void complete_run() noexcept {
        if (!pending_run_.has_value()) {
            return;
        }

        // 与 Prepare 相同，回调前先清空 pending，保证回调重入不会观察到 Busy。
        auto pending = std::move(*pending_run_);
        pending_run_.reset();
        execution_reservation_phase_ = MockExecutionReservationPhase::Terminal;
        auto& sink = pending.sink.sink();

        ++observations_.run_phase_callbacks;
        sink.on_phase(BoardRunPhaseEvent{
            pending.run, pending.generation, BoardRunPhase::Starting});
        ++observations_.run_phase_callbacks;
        sink.on_phase(BoardRunPhaseEvent{
            pending.run, pending.generation, BoardRunPhase::Acquiring});

        if (pending.scenario.run_behavior == MockRunBehavior::Fail) {
            // 延迟失败剧本只报告资源终态，不制造空数据块或部分正式观测。
            pending.delivery.retire();
            ++observations_.run_terminal_callbacks;
            sink.on_terminal(BoardRunTerminal{
                pending.run,
                pending.generation,
                RunTerminalKind::Failed,
                0U});
            return;
        }

        RunTerminalKind terminal_kind = RunTerminalKind::Completed;
        std::uint32_t delivered_chunks = 0U;
        for (std::size_t index = 0U;
             index < pending.manifest.required_observation_count;
             ++index) {
            const auto& required = pending.manifest.required_observations[index];
            if (pending.scenario.observation_behavior ==
                    MockObservationBehavior::OmitResponseButComplete &&
                required.wave == ReceiverWave::ResponseB) {
                continue;
            }
            const auto& samples = required.wave == ReceiverWave::IncidentA
                ? pending.scenario.incident_a
                : pending.scenario.response_b;
            const auto quality = required.wave == ReceiverWave::IncidentA
                ? pending.scenario.incident_quality
                : pending.scenario.response_quality;
            auto payload = pending.delivery.copy_fallback(
                samples, required.point_count);
            if (!payload.has_value()) {
                terminal_kind = RunTerminalKind::Failed;
                break;
            }
            ReceiverObservationChunk chunk{
                pending.manifest.id,
                pending.manifest.prepared_id,
                pending.run,
                pending.generation,
                ChunkSequence{index + 1U},
                required.wave,
                0U,
                std::move(payload).take_value(),
                quality};
            ++observations_.run_chunk_callbacks;
            ++delivered_chunks;
            if (sink.on_chunk(std::move(chunk)) !=
                ChunkIngressDisposition::Accepted) {
                terminal_kind = RunTerminalKind::Failed;
                break;
            }
        }

        // 先注销交付许可再发唯一终态；终态之后本 Run 不再持有交付预算。
        pending.delivery.retire();
        ++observations_.run_terminal_callbacks;
        sink.on_terminal(BoardRunTerminal{
            pending.run, pending.generation, terminal_kind, delivered_chunks});
    }

    void rebuild_capabilities() noexcept {
        const auto capability_revision =
            capabilities_.capability_revision == 0U
                ? 1U
                : capabilities_.capability_revision;
        const auto digest = core::StrongDigest{
            0xB04DCAFE00000000ULL ^ capability_revision ^
            (static_cast<std::uint64_t>(profile_.maximum_points) << 32U)};
        capabilities_ = CapabilitySnapshot{
            BoardContractVersion{1U, 0U},
            kMockSessionId,
            1U,
            capability_revision,
            1U,
            1U,
            digest,
            profile_.maximum_points};
    }

    MockCapabilityProfile profile_{};
    MockScenario scenario_{};
    CapabilitySnapshot initial_capabilities_{};
    CapabilitySnapshot capabilities_{};
    MockObservationSnapshot observations_{};
    VirtualDuration now_{0U};
    std::uint64_t next_prepared_id_{1U};
    std::optional<PendingPrepare> pending_prepare_{};
    std::optional<PendingRun> pending_run_{};
    BoardExecutionReservationId active_execution_reservation_{};
    MockExecutionReservationPhase execution_reservation_phase_{
        MockExecutionReservationPhase::None};
    /// 当前 execution reservation 唯一可供 Run 消费的 Prepare 成功身份。
    PreparedExecutionId active_prepared_id_{};
    /// 与 active_prepared_id_ 配对的 Manifest 摘要。
    core::StrongDigest active_manifest_digest_{};
    /// 当前 reservation 的实际执行事实；成功 Run 的形状只能从此处取得。
    PreparedExecutionManifest active_manifest_{};
    std::uint64_t next_execution_reservation_id_{1U};
};

core::Result<MockOpenedBoard, BoardError> make_opened_mock(
    const BoardOpenRequest& request,
    MockCapabilityProfile profile,
    MockScenario scenario) noexcept {
    if (request.selector != 1U || request.accepted_contract.major != 1U) {
        return core::Result<MockOpenedBoard, BoardError>::failure(
            BoardError{BoardErrc::Unsupported});
    }

    try {
        auto owner = std::make_unique<MockBoardSession>(profile, scenario);
        // control 是对 owner 所持同一对象的非 owning 视图，其寿命由 OpenedBoard 限定。
        auto* control = static_cast<MockBoardControl*>(owner.get());
        return core::Result<MockOpenedBoard, BoardError>::success(
            MockOpenedBoard{OpenedBoard{std::move(owner)}, control});
    } catch (const std::bad_alloc&) {
        // 异常只在适配器内部消化，BoardPort 边界统一返回类型化错误。
        return core::Result<MockOpenedBoard, BoardError>::failure(
            BoardError{BoardErrc::ResourceExhausted});
    } catch (...) {
        return core::Result<MockOpenedBoard, BoardError>::failure(
            BoardError{BoardErrc::ContractViolation});
    }
}

}  // namespace

MockBoardProvider::MockBoardProvider(
    MockCapabilityProfile profile,
    MockScenario scenario) noexcept
    : profile_(profile), scenario_(scenario) {}

core::Result<BoardInventorySnapshot, BoardError> MockBoardProvider::discover(
    const BoardDiscoveryRequest& request) noexcept {
    if (request.maximum_entries == 0U) {
        return core::Result<BoardInventorySnapshot, BoardError>::failure(
            BoardError{BoardErrc::InvalidIntent});
    }
    BoardInventorySnapshot inventory{};
    inventory.entries[0] = BoardInventoryEntry{1U};
    inventory.count = 1U;
    return core::Result<BoardInventorySnapshot, BoardError>::success(inventory);
}

core::Result<OpenedBoard, BoardError> MockBoardProvider::open(
    const BoardOpenRequest& request) noexcept {
    auto opened = make_opened_mock(request, profile_, scenario_);
    if (!opened) {
        return core::Result<OpenedBoard, BoardError>::failure(opened.error());
    }
    auto controlled = std::move(opened).take_value();
    return core::Result<OpenedBoard, BoardError>::success(std::move(controlled.board));
}

core::Result<MockOpenedBoard, BoardError> MockBoardProvider::open_controlled(
    const BoardOpenRequest& request) noexcept {
    return make_opened_mock(request, profile_, scenario_);
}

}  // namespace vna::board
