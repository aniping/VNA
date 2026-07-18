#include "vna/board/mock_board.hpp"

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

RunDeliveryGrant::RunDeliveryGrant(RunDeliveryGrant&& other) noexcept
    : grant_id_(other.grant_id_), valid_(other.valid_) {
    other.invalidate();
}

RunDeliveryGrant& RunDeliveryGrant::operator=(RunDeliveryGrant&& other) noexcept {
    if (this != &other) {
        grant_id_ = other.grant_id_;
        valid_ = other.valid_;
        other.invalidate();
    }
    return *this;
}

void RunDeliveryGrant::invalidate() noexcept {
    grant_id_ = 0U;
    valid_ = false;
}

AcquisitionChunkLease::AcquisitionChunkLease(
    const std::array<ComplexSample, kMaximumContractChunkSamples>& samples,
    std::size_t size) noexcept
    : samples_(samples),
      size_(size <= samples_.size() ? size : 0U),
      valid_(size > 0U && size <= samples_.size()) {}

AcquisitionChunkLease::AcquisitionChunkLease(AcquisitionChunkLease&& other) noexcept
    : samples_(other.samples_), size_(other.size_), valid_(other.valid_) {
    other.invalidate();
}

AcquisitionChunkLease& AcquisitionChunkLease::operator=(
    AcquisitionChunkLease&& other) noexcept {
    if (this != &other) {
        samples_ = other.samples_;
        size_ = other.size_;
        valid_ = other.valid_;
        other.invalidate();
    }
    return *this;
}

void AcquisitionChunkLease::invalidate() noexcept {
    size_ = 0U;
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
    PrepareCallId call{};
    SweepIntent intent{};
    PrepareAuthorization authorization;
    PrepareSinkRegistration sink;
    VirtualDuration due_at{0U};
};

struct PendingRun final {
    BoardRunId run{};
    RunGeneration generation{};
    PreparedStartToken prepared;
    StartAuthorization authorization;
    RunDeliveryGrant delivery;
    BoardRunSinkRegistration sink;
    MockScenario scenario{};
    VirtualDuration due_at{0U};
};

class MockBoardSession final : public BoardSession,
                               public BoardExecutionPort,
                               public MockBoardControl {
public:
    MockBoardSession(MockCapabilityProfile profile, MockScenario scenario) noexcept
        : profile_(profile), scenario_(scenario) {
        rebuild_capabilities();
    }

    BoardExecutionPort& execution() noexcept override { return *this; }
    const CapabilitySnapshot& initial_capabilities() const noexcept override {
        return capabilities_;
    }

    CapabilitySnapshot capabilities() const noexcept override { return capabilities_; }

    PrepareSubmission begin_prepare(
        PrepareCallId call,
        SweepIntent intent,
        PrepareAuthorization&& authorization,
        PrepareSinkRegistration&& sink) noexcept override {
        if (scenario_.prepare_behavior == MockPrepareBehavior::Reject) {
            ++observations_.rejected_prepare_calls;
            return PrepareRejected{
                BoardError{BoardErrc::Unsupported},
                ReclaimedPrepareInputs{
                    std::move(intent), std::move(authorization), std::move(sink)}};
        }

        const bool intent_is_valid = intent.point_count > 0U &&
            intent.point_count <= capabilities_.maximum_points &&
            intent.start_hz > 0.0 &&
            (intent.point_count == 1U
                 ? intent.stop_hz == intent.start_hz
                 : intent.stop_hz > intent.start_hz) &&
            intent.digest.valid();
        if (!intent_is_valid) {
            ++observations_.rejected_prepare_calls;
            return PrepareRejected{
                BoardError{BoardErrc::InvalidIntent},
                ReclaimedPrepareInputs{
                    std::move(intent), std::move(authorization), std::move(sink)}};
        }

        const bool authorization_matches = authorization.valid() &&
            authorization.session_id() == capabilities_.session_id &&
            authorization.session_epoch() == capabilities_.session_epoch &&
            authorization.capability_revision() == capabilities_.capability_revision &&
            authorization.topology_epoch() == capabilities_.topology_epoch &&
            authorization.operational_epoch() == capabilities_.operational_epoch &&
            authorization.intent_digest() == intent.digest;
        if (!authorization_matches) {
            ++observations_.rejected_prepare_calls;
            return PrepareRejected{
                BoardError{BoardErrc::AuthorizationMismatch},
                ReclaimedPrepareInputs{
                    std::move(intent), std::move(authorization), std::move(sink)}};
        }

        if (pending_prepare_.has_value()) {
            ++observations_.rejected_prepare_calls;
            return PrepareRejected{
                BoardError{BoardErrc::Busy},
                ReclaimedPrepareInputs{
                    std::move(intent), std::move(authorization), std::move(sink)}};
        }

        ++observations_.accepted_prepare_calls;
        pending_prepare_.emplace(PendingPrepare{
            call,
            std::move(intent),
            std::move(authorization),
            std::move(sink),
            now_ + scenario_.prepare_delay});
        return PrepareAccepted{call};
    }

    RunSubmission begin_run(
        BoardRunId run,
        RunGeneration generation,
        PreparedStartToken&& prepared,
        StartAuthorization&& authorization,
        RunDeliveryGrant&& delivery,
        BoardRunSinkRegistration&& sink) noexcept override {
        auto reject = [&](BoardErrc code) mutable -> RunSubmission {
            ++observations_.rejected_run_calls;
            return RunRejected{
                BoardError{code},
                ReclaimedRunInputs{
                    std::move(prepared),
                    std::move(authorization),
                    std::move(delivery),
                    std::move(sink)}};
        };

        if (scenario_.run_behavior == MockRunBehavior::Reject) {
            return reject(BoardErrc::Unsupported);
        }
        if (pending_run_.has_value()) {
            return reject(BoardErrc::Busy);
        }
        if (scenario_.point_count == 0U ||
            scenario_.point_count > kMaximumContractChunkSamples ||
            scenario_.point_count > capabilities_.maximum_points) {
            return reject(BoardErrc::ResourceExhausted);
        }

        const bool authorization_matches = prepared.valid() && authorization.valid() &&
            delivery.valid() && sink.valid() && run.valid() && generation.valid() &&
            prepared.session_id() == capabilities_.session_id &&
            authorization.session_id() == capabilities_.session_id &&
            authorization.prepared_id() == prepared.prepared_id() &&
            authorization.manifest_digest() == prepared.manifest_digest() &&
            authorization.operational_epoch() == capabilities_.operational_epoch &&
            authorization.continuation().valid() &&
            authorization.continuation().expires_at > now_;
        if (!authorization_matches) {
            return reject(BoardErrc::AuthorizationMismatch);
        }

        ++observations_.accepted_run_calls;
        pending_run_.emplace(PendingRun{
            run,
            generation,
            std::move(prepared),
            std::move(authorization),
            std::move(delivery),
            std::move(sink),
            scenario_,
            now_ + scenario_.run_delay});
        return RunAccepted{run, generation};
    }

    void load_profile(MockCapabilityProfile profile) noexcept override {
        profile_ = profile;
        rebuild_capabilities();
    }

    void load_scenario(MockScenario scenario) noexcept override { scenario_ = scenario; }

    void advance(VirtualDuration delta) noexcept override {
        now_ += delta;
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
    void complete_prepare() noexcept {
        if (!pending_prepare_.has_value()) {
            return;
        }

        auto pending = std::move(*pending_prepare_);
        pending_prepare_.reset();
        const auto prepared_id = PreparedExecutionId{next_prepared_id_++};
        const core::StrongDigest manifest_digest{
            pending.intent.digest.value ^ 0xA5A5A5A5A5A5A5A5ULL};
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
            pending.intent.stop_hz};
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

        auto pending = std::move(*pending_run_);
        pending_run_.reset();
        auto& sink = pending.sink.sink();

        ++observations_.run_phase_callbacks;
        sink.on_phase(BoardRunPhaseEvent{
            pending.run, pending.generation, BoardRunPhase::Starting});
        ++observations_.run_phase_callbacks;
        sink.on_phase(BoardRunPhaseEvent{
            pending.run, pending.generation, BoardRunPhase::Acquiring});

        const auto manifest_id = ManifestId{pending.prepared.prepared_id().value()};
        const auto prepared_id = pending.prepared.prepared_id();
        ReceiverObservationChunk incident{
            manifest_id,
            prepared_id,
            pending.run,
            pending.generation,
            ChunkSequence{1U},
            ReceiverWave::IncidentA,
            0U,
            AcquisitionChunkLease{
                pending.scenario.incident_a, pending.scenario.point_count},
            pending.scenario.incident_quality};
        ++observations_.run_chunk_callbacks;
        const auto incident_disposition = sink.on_chunk(std::move(incident));

        RunTerminalKind terminal_kind = RunTerminalKind::Completed;
        std::uint32_t delivered_chunks = 1U;
        if (incident_disposition == ChunkIngressDisposition::Accepted) {
            ReceiverObservationChunk response{
                manifest_id,
                prepared_id,
                pending.run,
                pending.generation,
                ChunkSequence{2U},
                ReceiverWave::ResponseB,
                0U,
                AcquisitionChunkLease{
                    pending.scenario.response_b, pending.scenario.point_count},
                pending.scenario.response_quality};
            ++observations_.run_chunk_callbacks;
            const auto response_disposition = sink.on_chunk(std::move(response));
            delivered_chunks = 2U;
            if (response_disposition != ChunkIngressDisposition::Accepted) {
                terminal_kind = RunTerminalKind::Failed;
            }
        } else {
            terminal_kind = RunTerminalKind::Failed;
        }

        pending.delivery.retire();
        ++observations_.run_terminal_callbacks;
        sink.on_terminal(BoardRunTerminal{
            pending.run, pending.generation, terminal_kind, delivered_chunks});
    }

    void rebuild_capabilities() noexcept {
        capabilities_ = CapabilitySnapshot{
            BoardContractVersion{1U, 0U},
            kMockSessionId,
            1U,
            1U,
            1U,
            1U,
            core::StrongDigest{0xB04DCAFEU},
            profile_.maximum_points};
    }

    MockCapabilityProfile profile_{};
    MockScenario scenario_{};
    CapabilitySnapshot capabilities_{};
    MockObservationSnapshot observations_{};
    VirtualDuration now_{0U};
    std::uint64_t next_prepared_id_{1U};
    std::optional<PendingPrepare> pending_prepare_{};
    std::optional<PendingRun> pending_run_{};
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
        auto* control = static_cast<MockBoardControl*>(owner.get());
        return core::Result<MockOpenedBoard, BoardError>::success(
            MockOpenedBoard{OpenedBoard{std::move(owner)}, control});
    } catch (const std::bad_alloc&) {
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
