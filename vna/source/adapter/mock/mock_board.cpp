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

DiscardPreparedSinkRegistration::DiscardPreparedSinkRegistration(
    DiscardPreparedSinkRegistration&& other) noexcept
    : sink_(other.sink_) {
    other.sink_ = nullptr;
}

DiscardPreparedSinkRegistration&
DiscardPreparedSinkRegistration::operator=(
    DiscardPreparedSinkRegistration&& other) noexcept {
    if (this != &other) {
        sink_ = other.sink_;
        other.sink_ = nullptr;
    }
    return *this;
}

namespace {

constexpr VirtualDuration kMinimumMockRunDuration{300U};
constexpr VirtualDuration kMaximumMockRunDuration{400U};

struct ResolvedDeliveryPlan final {
    std::array<MockChunkDelivery, kMaximumRunChunks> deliveries{};
    std::uint32_t count{0U};
    bool valid{false};
};

const PreparedObservationSpec* find_required_observation(
    const PreparedExecutionManifest& manifest,
    const MockChunkDelivery& delivery) noexcept {
    for (std::size_t index = 0U;
         index < manifest.required_observation_count;
         ++index) {
        const auto& required = manifest.required_observations[index];
        if (required.source_state == delivery.source_state &&
            required.receiver_path == delivery.receiver_path &&
            required.wave == delivery.wave) {
            return &required;
        }
    }
    return nullptr;
}

ResolvedDeliveryPlan resolve_delivery_plan(
    const PreparedExecutionManifest& manifest,
    const MockScenario& scenario) noexcept {
    ResolvedDeliveryPlan result{};
    if (scenario.run_duration < kMinimumMockRunDuration ||
        scenario.run_duration > kMaximumMockRunDuration ||
        scenario.chunk_delivery_count > result.deliveries.size()) {
        return result;
    }

    if (scenario.run_behavior == MockRunBehavior::Fail) {
        result.valid = true;
        return result;
    }

    if (scenario.chunk_delivery_count > 0U) {
        result.count = scenario.chunk_delivery_count;
        for (std::size_t index = 0U; index < result.count; ++index) {
            result.deliveries[index] = scenario.chunk_deliveries[index];
        }
    } else {
        // 默认剧本仍完全由 Manifest 决定形状；每项观测按 64 点上限切块，
        // OmitResponse 只改变交付集合，供后续完整覆盖校验证明 terminal 不充分。
        std::uint32_t total_chunks{0U};
        for (std::size_t index = 0U;
             index < manifest.required_observation_count;
             ++index) {
            const auto& required = manifest.required_observations[index];
            if (scenario.observation_behavior ==
                    MockObservationBehavior::OmitResponseButComplete &&
                required.wave == ReceiverWave::ResponseB) {
                continue;
            }
            total_chunks += static_cast<std::uint32_t>(
                (required.point_count + kMaximumContractChunkSamples - 1U) /
                kMaximumContractChunkSamples);
        }
        if (total_chunks == 0U || total_chunks > result.deliveries.size()) {
            return result;
        }

        for (std::size_t index = 0U;
             index < manifest.required_observation_count;
             ++index) {
            const auto& required = manifest.required_observations[index];
            if (scenario.observation_behavior ==
                    MockObservationBehavior::OmitResponseButComplete &&
                required.wave == ReceiverWave::ResponseB) {
                continue;
            }
            std::uint32_t point_begin{0U};
            while (point_begin < required.point_count) {
                const auto remaining = required.point_count - point_begin;
                const auto point_count = static_cast<std::uint32_t>(
                    remaining < kMaximumContractChunkSamples
                        ? remaining
                        : kMaximumContractChunkSamples);
                const auto offset =
                    scenario.run_duration * (result.count + 1U) /
                    (total_chunks + 1U);
                result.deliveries[result.count] = MockChunkDelivery{
                    required.source_state,
                    required.receiver_path,
                    required.wave,
                    point_begin,
                    point_count,
                    offset,
                    required.wave == ReceiverWave::IncidentA
                        ? scenario.incident_quality
                        : scenario.response_quality};
                ++result.count;
                point_begin += point_count;
            }
        }
    }

    for (std::size_t index = 0U; index < result.count; ++index) {
        const auto& delivery = result.deliveries[index];
        const auto* required = find_required_observation(manifest, delivery);
        if (required == nullptr || !delivery.source_state.valid() ||
            !delivery.receiver_path.valid() || delivery.point_count == 0U ||
            delivery.point_count > kMaximumContractChunkSamples ||
            delivery.offset >= scenario.run_duration ||
            delivery.point_begin >= kMaximumMockSweepPoints ||
            delivery.point_count >
                kMaximumMockSweepPoints - delivery.point_begin) {
            return ResolvedDeliveryPlan{};
        }
    }
    if (scenario.maximum_chunks_before_completed_terminal > result.count) {
        return ResolvedDeliveryPlan{};
    }
    result.valid = true;
    return result;
}

struct PendingPrepare final {
    // begin_prepare() 接受后，Mock 独占保存全部 move-only 输入直到 due_at。
    PrepareCallId call{};
    SweepIntent intent{};
    PrepareAuthorization authorization;
    PrepareSinkRegistration sink;
    MockPrepareBehavior behavior{MockPrepareBehavior::Succeed};
    MockManifestBehavior manifest_behavior{MockManifestBehavior::MatchIntent};
    VirtualDuration due_at{0U};
};

struct PendingDiscard final {
    // begin_discard_prepared() 接受后，Mock 独占 token 与 sink 到 cleanup terminal。
    PreparedStartToken prepared;
    DiscardPreparedSinkRegistration sink;
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
    /// 接受时冻结的 Manifest-derived 或显式确定性交付计划。
    std::array<MockChunkDelivery, kMaximumRunChunks> deliveries{};
    std::uint32_t delivery_count{0U};
    /// 每项计划是否已在对应 offset 到期时尝试交付。
    std::array<bool, kMaximumRunChunks> delivery_attempted{};
    VirtualDuration accepted_at{0U};
    VirtualDuration terminal_at{0U};
    std::uint32_t delivered_chunks{0U};
    bool phases_delivered{false};
    bool delivery_failed{false};
};

enum class MockExecutionReservationPhase {
    None,
    Reserved,
    Preparing,
    Prepared,
    RunRejectedAwaitingDiscard,
    Discarding,
    Running,
    Terminal
};

class MockBoardSession final : public BoardSession,
                               public BoardExecutionPort,
                               public MockBoardControl {
public:
    MockBoardSession(
        BoardSessionId session_id,
        MockCapabilityProfile profile,
        MockScenario scenario) noexcept
        : session_id_(session_id), profile_(profile), scenario_(scenario) {
        rebuild_capabilities();
        initial_capabilities_ = capabilities_;
    }

    BoardExecutionPort& execution() noexcept override { return *this; }
    const CapabilitySnapshot& initial_capabilities() const noexcept override {
        return initial_capabilities_;
    }

    CapabilitySnapshot capabilities() const noexcept override { return capabilities_; }
    std::uint64_t monotonic_tick() const noexcept override { return now_; }

    void isolate_contract_violation(
        BoardContractViolation violation) noexcept override {
        if (session_state_ == MockSessionState::IsolatedContractViolation) {
            return;
        }
        isolated_violation_ = violation;
        session_state_ = MockSessionState::IsolatedContractViolation;
        ++observations_.isolated_session_transitions;
    }

    core::Result<BoardExecutionReservation, BoardError>
    reserve_execution() noexcept override {
        if (session_state_ == MockSessionState::IsolatedContractViolation) {
            ++observations_.rejected_execution_reservations;
            ++observations_.rejected_isolated_execution_reservations;
            return core::Result<BoardExecutionReservation, BoardError>::failure(
                BoardError{BoardErrc::ContractViolation});
        }
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
            scenario_.prepare_behavior,
            scenario_.manifest_behavior,
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
                    MockExecutionReservationPhase::RunRejectedAwaitingDiscard;
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
             active_manifest_.actual_point_count > kMaximumMockSweepPoints ||
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
        const auto delivery_plan =
            resolve_delivery_plan(active_manifest_, scenario_);
        if (!delivery_plan.valid) {
            return reject(BoardErrc::ContractViolation, true);
        }
        // 不用 Mock 已知的显式剧本数量替代 Board 契约：合法容量只需覆盖
        // Manifest 必需块。超量剧本模拟真实底软在 Run callback 期间违约，
        // 由 copy_fallback()/Ingress 的有界边界终止整轮，而不是预先藏掉故障。
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
            delivery_plan.deliveries,
            delivery_plan.count,
            {},
            now_,
            now_ + scenario_.run_duration,
            0U,
            false,
            false});
        return RunAccepted{run, generation};
    }

    DiscardPreparedSubmission begin_discard_prepared(
        const BoardExecutionReservation& reservation,
        PreparedStartToken&& prepared,
        DiscardPreparedRequest request,
        DiscardPreparedSinkRegistration&& sink) noexcept override {
        (void)request;
        auto reject = [&](BoardErrc code) mutable -> DiscardPreparedSubmission {
            ++observations_.rejected_discard_calls;
            return DiscardPreparedRejected{
                BoardError{code},
                ReclaimedDiscardPreparedInputs{
                    std::move(prepared), std::move(sink)}};
        };

        const bool phase_allows_discard =
            execution_reservation_phase_ ==
                MockExecutionReservationPhase::Prepared ||
            execution_reservation_phase_ ==
                MockExecutionReservationPhase::RunRejectedAwaitingDiscard;
        if (!owns_execution_reservation(reservation) ||
            reservation.id() != active_execution_reservation_ ||
            !phase_allows_discard || pending_discard_.has_value()) {
            return reject(BoardErrc::ContractViolation);
        }
        if (!prepared.valid() || !sink.valid() ||
            prepared.session_id() != capabilities_.session_id ||
            prepared.prepared_id() != active_prepared_id_ ||
            prepared.manifest_digest() != active_manifest_digest_) {
            return reject(BoardErrc::AuthorizationMismatch);
        }

        const auto prepared_id = prepared.prepared_id();
        ++observations_.accepted_discard_calls;
        execution_reservation_phase_ = MockExecutionReservationPhase::Discarding;
        pending_discard_.emplace(PendingDiscard{
            std::move(prepared),
            std::move(sink),
            now_ + scenario_.discard_delay});
        return DiscardPreparedAccepted{prepared_id};
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

        if (pending_discard_.has_value() && pending_discard_->due_at <= now_) {
            complete_discard();
        }

        if (pending_run_.has_value()) {
            progress_run();
        }
    }

    MockObservationSnapshot observations() const noexcept override {
        return observations_;
    }

    MockSessionState session_state() const noexcept override {
        return session_state_;
    }

private:
    void release_execution_reservation(
        BoardExecutionReservationId id) noexcept override {
        // generation-bound 身份避免旧租约析构释放后来复用的新执行槽；若调用者
        // 违反契约，在 Accepted terminal 前销毁租约，则宁可保持槽位占用也不允许
        // 新请求复用仍持有 callback sink 的底层容量。
        if (active_execution_reservation_ == id &&
            !pending_prepare_.has_value() && !pending_discard_.has_value() &&
            !pending_run_.has_value() &&
            (execution_reservation_phase_ ==
                 MockExecutionReservationPhase::Reserved ||
             execution_reservation_phase_ ==
                 MockExecutionReservationPhase::Terminal)) {
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
        if (pending.behavior == MockPrepareBehavior::Fail) {
            // PrepareFailed 自带 cleanup evidence，因此回调前可结束 Mock 的
            // Prepare 资源义务；上层仍必须等该 terminal 后才能提交 Failed。
            execution_reservation_phase_ = MockExecutionReservationPhase::Terminal;
            PrepareTerminal terminal = PrepareFailed{
                PrepareCleanupEvidence{},
                BoardError{BoardErrc::ResourceExhausted}};
            ++observations_.prepare_terminal_callbacks;
            pending.sink.sink().on_terminal(std::move(terminal));
            return;
        }
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
                    ReceiverWave::IncidentA,
                    pending.intent.point_count,
                    SourceStateId{1U},
                    ReceiverPathId{1U}},
                PreparedObservationSpec{
                    ReceiverWave::ResponseB,
                    pending.intent.point_count,
                    SourceStateId{1U},
                    ReceiverPathId{2U}}},
            2U};
        switch (pending.manifest_behavior) {
            case MockManifestBehavior::MatchIntent:
                break;
            case MockManifestBehavior::StaleCapabilityRevision:
                ++manifest.capability_revision;
                break;
            case MockManifestBehavior::MismatchedSession:
                manifest.session_id = BoardSessionId{
                    manifest.session_id.value() + 1U};
                break;
            case MockManifestBehavior::ExpandedPointEnvelope:
                ++manifest.actual_point_count;
                manifest.required_observations[0U].point_count =
                    manifest.actual_point_count;
                manifest.required_observations[1U].point_count =
                    manifest.actual_point_count;
                break;
        }
        active_manifest_ = manifest;
        // 清单摘要把后续 PreparedStartToken/StartAuthorization 绑定到本次 Prepare。
        PrepareTerminal terminal = PrepareSucceeded{PreparedExecution{
            PreparedStartToken{capabilities_.session_id, prepared_id, manifest_digest},
            PreparedManifestLease{std::move(manifest)}}};
        ++observations_.prepare_terminal_callbacks;
        pending.sink.sink().on_terminal(std::move(terminal));
    }

    void complete_discard() noexcept {
        if (!pending_discard_.has_value()) {
            return;
        }

        auto pending = std::move(*pending_discard_);
        pending_discard_.reset();
        const auto prepared_id = pending.prepared.prepared_id();
        execution_reservation_phase_ = MockExecutionReservationPhase::Terminal;
        active_prepared_id_ = PreparedExecutionId{};
        active_manifest_digest_ = core::StrongDigest{};
        active_manifest_ = PreparedExecutionManifest{};
        ++observations_.discard_terminal_callbacks;
        pending.sink.sink().on_terminal(DiscardPreparedTerminal{
            prepared_id,
            DiscardPreparedTerminalKind::Discarded,
            BoardError{BoardErrc::ContractViolation}});
    }

    void progress_run() noexcept {
        if (!pending_run_.has_value()) {
            return;
        }

        auto& pending = *pending_run_;
        auto& sink = pending.sink.sink();
        if (!pending.phases_delivered) {
            // Accepted 仅登记任务；首次显式推进虚拟时间才发阶段，绝不内联回调。
            pending.phases_delivered = true;
            ++observations_.run_phase_callbacks;
            sink.on_phase(BoardRunPhaseEvent{
                pending.run, pending.generation, BoardRunPhase::Starting});
            ++observations_.run_phase_callbacks;
            sink.on_phase(BoardRunPhaseEvent{
                pending.run, pending.generation, BoardRunPhase::Acquiring});
        }

        if (pending.scenario.run_behavior == MockRunBehavior::Succeed &&
            !pending.delivery_failed) {
            const auto delivery_limit =
                pending.scenario.maximum_chunks_before_completed_terminal == 0U
                ? pending.delivery_count
                : pending.scenario.maximum_chunks_before_completed_terminal;
            while (!pending.delivery_failed &&
                   pending.delivered_chunks < delivery_limit) {
                // advance() 可以一次跨过多个 offset；仍须按虚拟事件时刻排序，
                // 相同 offset 再按剧本下标稳定排序，避免推进粒度改变 callback 序列。
                std::size_t selected = pending.delivery_count;
                for (std::size_t index = 0U;
                     index < pending.delivery_count;
                     ++index) {
                    if (pending.delivery_attempted[index] ||
                        pending.accepted_at +
                                pending.deliveries[index].offset >
                            now_) {
                        continue;
                    }
                    if (selected == pending.delivery_count ||
                        pending.deliveries[index].offset <
                            pending.deliveries[selected].offset) {
                        selected = index;
                    }
                }
                if (selected == pending.delivery_count) {
                    break;
                }

                const auto index = selected;
                const auto& delivery = pending.deliveries[index];
                // 尝试标志必须先于回调设置，防止回调重入造成同一计划项重复交付。
                pending.delivery_attempted[index] = true;
                std::array<ComplexSample, kMaximumContractChunkSamples>
                    source_chunk{};
                const auto& source = delivery.wave == ReceiverWave::IncidentA
                    ? pending.scenario.incident_a
                    : pending.scenario.response_b;
                for (std::size_t point = 0U;
                     point < delivery.point_count;
                     ++point) {
                    source_chunk[point] = source[delivery.point_begin + point];
                }
                auto payload = pending.delivery.copy_fallback(
                    source_chunk, delivery.point_count);
                if (!payload.has_value()) {
                    ++observations_.failed_buffer_copies;
                    pending.delivery_failed = true;
                    break;
                }
                const auto sequence = ChunkSequence{
                    static_cast<std::uint64_t>(pending.delivered_chunks) + 1U};
                auto deliver = [&](AcquisitionChunkLease&& lease) {
                    ReceiverObservationChunk chunk{
                        pending.manifest.id,
                        pending.manifest.prepared_id,
                        pending.run,
                        pending.generation,
                        sequence,
                        delivery.wave,
                        delivery.point_begin,
                        std::move(lease),
                        delivery.quality,
                        delivery.source_state,
                        delivery.receiver_path};
                    // 身份故障只改写首次回调的固定大小 header；payload 仍沿同一
                    // move-only 路径交给 Acquisition，由接收方决定协议拒绝。
                    if (pending.delivered_chunks == 0U) {
                        switch (pending.scenario.contract_fault) {
                            case MockRunContractFault::WrongManifest:
                                chunk.manifest_id = ManifestId{
                                    chunk.manifest_id.value() + 1000U};
                                break;
                            case MockRunContractFault::WrongPreparedExecution:
                                chunk.prepared_id = PreparedExecutionId{
                                    chunk.prepared_id.value() + 1000U};
                                break;
                            case MockRunContractFault::WrongBoardRunId:
                                chunk.run_id = BoardRunId{
                                    chunk.run_id.value() + 1000U};
                                break;
                            case MockRunContractFault::WrongGeneration:
                                chunk.run_generation = RunGeneration{
                                    chunk.run_generation.value() + 1000U};
                                break;
                            case MockRunContractFault::None:
                            case MockRunContractFault::MultipleTerminal:
                            case MockRunContractFault::CallbackAfterTerminal:
                                break;
                        }
                    }
                    ++observations_.run_chunk_callbacks;
                    ++pending.delivered_chunks;
                    const auto disposition =
                        sink.on_chunk(std::move(chunk));
                    if (!chunk.payload.valid()) {
                        ++observations_.consumed_chunk_payloads;
                    }
                    if (disposition != ChunkIngressDisposition::Accepted) {
                        pending.delivery_failed = true;
                    }
                };
                auto lease = std::move(payload).take_value();
                if (delivery.payload_behavior ==
                    MockChunkPayloadBehavior::InvalidLease) {
                    // 保留真实 owner 到 callback 返回，但把 moved-from lease 作为
                    // 非合规 payload 交付，以确定性触发 Ingress 协议拒绝。
                    auto retained = std::move(lease);
                    deliver(std::move(lease));
                } else {
                    deliver(std::move(lease));
                }
                if (pending.scenario.driver_buffer_behavior ==
                    MockDriverBufferBehavior::ReuseImmediatelyAfterCallback) {
                    // 只有 on_chunk() 已经返回后才覆写，精确模拟“底软 callback
                    // 内存立即复用”。若正式数据仍引用 source_chunk，验收值会被毒化。
                    for (auto& sample : source_chunk) {
                        sample = ComplexSample{-99999.0F, 99999.0F};
                    }
                    ++observations_.reused_driver_buffers;
                }
            }
        }

        if (now_ < pending.terminal_at) {
            return;
        }

        // 与 Prepare 相同，终态回调前先清空 pending。CallbackAfterTerminal 剧本
        // 会先签发一份独立 lease，再注销 grant，证明违规回调也只有一个 payload
        // owner，且其析构不依赖 grant 继续存活。
        auto completed = std::move(*pending_run_);
        pending_run_.reset();
        execution_reservation_phase_ = MockExecutionReservationPhase::Terminal;
        std::optional<AcquisitionChunkLease> post_terminal_payload{};
        if (completed.scenario.contract_fault ==
                MockRunContractFault::CallbackAfterTerminal &&
            completed.scenario.run_behavior == MockRunBehavior::Succeed) {
            std::array<ComplexSample, kMaximumContractChunkSamples> sample{};
            sample[0U] = completed.scenario.incident_a[0U];
            auto copied = completed.delivery.copy_fallback(sample, 1U);
            if (copied.has_value()) {
                post_terminal_payload.emplace(
                    std::move(copied).take_value());
            }
        }
        completed.delivery.retire();
        const auto terminal_kind =
            completed.scenario.run_behavior == MockRunBehavior::Succeed &&
                !completed.delivery_failed
            ? RunTerminalKind::Completed
            : RunTerminalKind::Failed;
        ++observations_.run_terminal_callbacks;
        completed.sink.sink().on_terminal(BoardRunTerminal{
            completed.run,
            completed.generation,
            terminal_kind,
            completed.delivered_chunks});
        if (completed.scenario.contract_fault ==
            MockRunContractFault::MultipleTerminal) {
            ++observations_.run_terminal_callbacks;
            completed.sink.sink().on_terminal(BoardRunTerminal{
                completed.run,
                completed.generation,
                terminal_kind,
                completed.delivered_chunks});
        } else if (completed.scenario.contract_fault ==
                       MockRunContractFault::CallbackAfterTerminal &&
                   post_terminal_payload.has_value()) {
            const auto& observation =
                completed.manifest.required_observations[0U];
            ++observations_.run_chunk_callbacks;
            ReceiverObservationChunk chunk{
                completed.manifest.id,
                completed.manifest.prepared_id,
                completed.run,
                completed.generation,
                ChunkSequence{
                    static_cast<std::uint64_t>(completed.delivered_chunks) +
                    1U},
                observation.wave,
                0U,
                std::move(*post_terminal_payload),
                completed.scenario.incident_quality,
                observation.source_state,
                observation.receiver_path};
            (void)completed.sink.sink().on_chunk(std::move(chunk));
            if (!chunk.payload.valid()) {
                ++observations_.consumed_chunk_payloads;
            }
        }
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
            session_id_,
            1U,
            capability_revision,
            1U,
            1U,
            digest,
            profile_.maximum_points};
    }

    BoardSessionId session_id_{};
    MockCapabilityProfile profile_{};
    MockScenario scenario_{};
    CapabilitySnapshot initial_capabilities_{};
    CapabilitySnapshot capabilities_{};
    MockObservationSnapshot observations_{};
    VirtualDuration now_{0U};
    std::uint64_t next_prepared_id_{1U};
    std::optional<PendingPrepare> pending_prepare_{};
    std::optional<PendingDiscard> pending_discard_{};
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
    MockSessionState session_state_{MockSessionState::Healthy};
    /// 首次隔离原因的固定大小副本；不持有 callback 或恢复能力。
    std::optional<BoardContractViolation> isolated_violation_{};
};

core::Result<MockOpenedBoard, BoardError> make_opened_mock(
    const BoardOpenRequest& request,
    BoardSessionId session_id,
    MockCapabilityProfile profile,
    MockScenario scenario) noexcept {
    if (request.selector != 1U || request.accepted_contract.major != 1U) {
        return core::Result<MockOpenedBoard, BoardError>::failure(
            BoardError{BoardErrc::Unsupported});
    }

    try {
        auto owner = std::make_unique<MockBoardSession>(
            session_id, profile, scenario);
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
    if (next_session_id_ == 0U) {
        return core::Result<OpenedBoard, BoardError>::failure(
            BoardError{BoardErrc::ResourceExhausted});
    }
    auto opened = make_opened_mock(
        request, BoardSessionId{next_session_id_}, profile_, scenario_);
    if (!opened) {
        return core::Result<OpenedBoard, BoardError>::failure(opened.error());
    }
    ++next_session_id_;
    auto controlled = std::move(opened).take_value();
    return core::Result<OpenedBoard, BoardError>::success(std::move(controlled.board));
}

core::Result<MockOpenedBoard, BoardError> MockBoardProvider::open_controlled(
    const BoardOpenRequest& request) noexcept {
    if (next_session_id_ == 0U) {
        return core::Result<MockOpenedBoard, BoardError>::failure(
            BoardError{BoardErrc::ResourceExhausted});
    }
    auto opened = make_opened_mock(
        request, BoardSessionId{next_session_id_}, profile_, scenario_);
    if (opened.has_value()) {
        ++next_session_id_;
    }
    return opened;
}

}  // namespace vna::board
