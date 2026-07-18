#pragma once

#include "vna/core/result.hpp"
#include "vna/core/strong_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>

namespace vna::board {

using BoardSessionId = core::StrongId<struct BoardSessionIdTag>;
using PrepareCallId = core::StrongId<struct PrepareCallIdTag>;
using PreparedExecutionId = core::StrongId<struct PreparedExecutionIdTag>;
using ManifestId = core::StrongId<struct ManifestIdTag>;
using BoardRunId = core::StrongId<struct BoardRunIdTag>;
using RunGeneration = core::StrongId<struct RunGenerationTag>;
using ChunkSequence = core::StrongId<struct ChunkSequenceTag>;

struct BoardContractVersion final {
    std::uint16_t major{1U};
    std::uint16_t minor{0U};
};

enum class BoardErrc {
    InvalidIntent,
    Unsupported,
    StaleSessionEpoch,
    StaleCapability,
    StaleTopologyEpoch,
    StaleOperationalEpoch,
    AuthorizationMismatch,
    Busy,
    ResourceExhausted,
    ContractViolation,
    Closed
};

struct BoardError final {
    BoardErrc code{BoardErrc::ContractViolation};
};

struct BoardInventoryEntry final {
    std::uint32_t selector{0U};
};

struct BoardInventorySnapshot final {
    std::array<BoardInventoryEntry, 4U> entries{};
    std::size_t count{0U};
};

struct BoardDiscoveryRequest final {
    std::size_t maximum_entries{1U};
};

struct BoardOpenRequest final {
    std::uint32_t selector{0U};
    BoardContractVersion accepted_contract{};
};

struct CapabilitySnapshot final {
    BoardContractVersion contract{};
    BoardSessionId session_id{};
    std::uint64_t session_epoch{1U};
    std::uint64_t capability_revision{1U};
    std::uint64_t topology_epoch{1U};
    std::uint64_t operational_epoch{1U};
    core::StrongDigest digest{};
    std::uint32_t maximum_points{0U};
};

struct SweepIntent final {
    std::uint32_t point_count{0U};
    double start_hz{0.0};
    double stop_hz{0.0};
    core::StrongDigest digest{};
};

class PrepareAuthorization final {
public:
    static PrepareAuthorization issue(
        BoardSessionId session_id,
        std::uint64_t session_epoch,
        std::uint64_t capability_revision,
        std::uint64_t topology_epoch,
        std::uint64_t operational_epoch,
        core::StrongDigest intent_digest) noexcept;

    PrepareAuthorization(PrepareAuthorization&& other) noexcept;
    PrepareAuthorization& operator=(PrepareAuthorization&& other) noexcept;
    PrepareAuthorization(const PrepareAuthorization&) = delete;
    PrepareAuthorization& operator=(const PrepareAuthorization&) = delete;

    bool valid() const noexcept { return valid_; }
    BoardSessionId session_id() const noexcept { return session_id_; }
    std::uint64_t session_epoch() const noexcept { return session_epoch_; }
    std::uint64_t capability_revision() const noexcept { return capability_revision_; }
    std::uint64_t topology_epoch() const noexcept { return topology_epoch_; }
    std::uint64_t operational_epoch() const noexcept { return operational_epoch_; }
    core::StrongDigest intent_digest() const noexcept { return intent_digest_; }

private:
    PrepareAuthorization(
        BoardSessionId session_id,
        std::uint64_t session_epoch,
        std::uint64_t capability_revision,
        std::uint64_t topology_epoch,
        std::uint64_t operational_epoch,
        core::StrongDigest intent_digest) noexcept;

    void invalidate() noexcept;

    BoardSessionId session_id_{};
    std::uint64_t session_epoch_{0U};
    std::uint64_t capability_revision_{0U};
    std::uint64_t topology_epoch_{0U};
    std::uint64_t operational_epoch_{0U};
    core::StrongDigest intent_digest_{};
    bool valid_{false};
};

struct PreparedExecutionManifest final {
    ManifestId id{};
    PreparedExecutionId prepared_id{};
    BoardSessionId session_id{};
    std::uint64_t session_epoch{0U};
    std::uint64_t capability_revision{0U};
    std::uint64_t topology_epoch{0U};
    std::uint64_t operational_epoch{0U};
    core::StrongDigest intent_digest{};
    core::StrongDigest manifest_digest{};
    std::uint32_t actual_point_count{0U};
    double actual_start_hz{0.0};
    double actual_stop_hz{0.0};
};

class PreparedStartToken final {
public:
    PreparedStartToken(
        BoardSessionId session_id,
        PreparedExecutionId prepared_id,
        core::StrongDigest manifest_digest) noexcept;
    PreparedStartToken(PreparedStartToken&& other) noexcept;
    PreparedStartToken& operator=(PreparedStartToken&& other) noexcept;
    PreparedStartToken(const PreparedStartToken&) = delete;
    PreparedStartToken& operator=(const PreparedStartToken&) = delete;

    bool valid() const noexcept { return valid_; }
    BoardSessionId session_id() const noexcept { return session_id_; }
    PreparedExecutionId prepared_id() const noexcept { return prepared_id_; }
    core::StrongDigest manifest_digest() const noexcept { return manifest_digest_; }

private:
    void invalidate() noexcept;

    BoardSessionId session_id_{};
    PreparedExecutionId prepared_id_{};
    core::StrongDigest manifest_digest_{};
    bool valid_{false};
};

class PreparedManifestLease final {
public:
    explicit PreparedManifestLease(PreparedExecutionManifest manifest) noexcept
        : manifest_(std::move(manifest)) {}
    PreparedManifestLease(PreparedManifestLease&&) noexcept = default;
    PreparedManifestLease& operator=(PreparedManifestLease&&) noexcept = default;
    PreparedManifestLease(const PreparedManifestLease&) = delete;
    PreparedManifestLease& operator=(const PreparedManifestLease&) = delete;

    const PreparedExecutionManifest& manifest() const noexcept { return manifest_; }

private:
    PreparedExecutionManifest manifest_{};
};

struct PreparedExecution final {
    PreparedStartToken start_token;
    PreparedManifestLease manifest;
};

struct PrepareSucceeded final {
    PreparedExecution execution;
};

struct PrepareCleanupEvidence final {};
struct PrepareFailed final {
    PrepareCleanupEvidence cleanup;
    BoardError error;
};

struct BoardPrepareDrainOwner final {};
struct PrepareDraining final {
    BoardPrepareDrainOwner owner;
};

using PrepareTerminal = std::variant<PrepareSucceeded, PrepareFailed, PrepareDraining>;

class PrepareSink {
public:
    virtual ~PrepareSink() = default;
    virtual void on_terminal(PrepareTerminal&& terminal) noexcept = 0;
};

class PrepareSinkRegistration final {
public:
    explicit PrepareSinkRegistration(PrepareSink& sink) noexcept : sink_(&sink) {}
    PrepareSinkRegistration(PrepareSinkRegistration&& other) noexcept;
    PrepareSinkRegistration& operator=(PrepareSinkRegistration&& other) noexcept;
    PrepareSinkRegistration(const PrepareSinkRegistration&) = delete;
    PrepareSinkRegistration& operator=(const PrepareSinkRegistration&) = delete;

    bool valid() const noexcept { return sink_ != nullptr; }
    PrepareSink& sink() const noexcept { return *sink_; }

private:
    PrepareSink* sink_{nullptr};
};

struct PrepareAccepted final {
    PrepareCallId call{};
};

struct ReclaimedPrepareInputs final {
    SweepIntent intent{};
    PrepareAuthorization authorization;
    PrepareSinkRegistration sink;
};

struct PrepareRejected final {
    BoardError error{};
    ReclaimedPrepareInputs reclaimed;
};

using PrepareSubmission = std::variant<PrepareAccepted, PrepareRejected>;

struct AcquisitionContinuationAttestation final {
    core::StrongDigest digest{};
    std::uint64_t expires_at{0U};

    bool valid() const noexcept { return digest.valid() && expires_at != 0U; }
};

class StartAuthorization final {
public:
    static StartAuthorization issue(
        BoardSessionId session_id,
        PreparedExecutionId prepared_id,
        core::StrongDigest manifest_digest,
        std::uint64_t operational_epoch,
        AcquisitionContinuationAttestation continuation) noexcept;

    StartAuthorization(StartAuthorization&& other) noexcept;
    StartAuthorization& operator=(StartAuthorization&& other) noexcept;
    StartAuthorization(const StartAuthorization&) = delete;
    StartAuthorization& operator=(const StartAuthorization&) = delete;

    bool valid() const noexcept { return valid_; }
    BoardSessionId session_id() const noexcept { return session_id_; }
    PreparedExecutionId prepared_id() const noexcept { return prepared_id_; }
    core::StrongDigest manifest_digest() const noexcept { return manifest_digest_; }
    std::uint64_t operational_epoch() const noexcept { return operational_epoch_; }
    const AcquisitionContinuationAttestation& continuation() const noexcept {
        return continuation_;
    }

private:
    StartAuthorization(
        BoardSessionId session_id,
        PreparedExecutionId prepared_id,
        core::StrongDigest manifest_digest,
        std::uint64_t operational_epoch,
        AcquisitionContinuationAttestation continuation) noexcept;
    void invalidate() noexcept;

    BoardSessionId session_id_{};
    PreparedExecutionId prepared_id_{};
    core::StrongDigest manifest_digest_{};
    std::uint64_t operational_epoch_{0U};
    AcquisitionContinuationAttestation continuation_{};
    bool valid_{false};
};

class RunDeliveryGrant final {
public:
    explicit RunDeliveryGrant(std::uint64_t grant_id) noexcept
        : grant_id_(grant_id), valid_(grant_id != 0U) {}
    RunDeliveryGrant(RunDeliveryGrant&& other) noexcept;
    RunDeliveryGrant& operator=(RunDeliveryGrant&& other) noexcept;
    RunDeliveryGrant(const RunDeliveryGrant&) = delete;
    RunDeliveryGrant& operator=(const RunDeliveryGrant&) = delete;

    bool valid() const noexcept { return valid_; }
    std::uint64_t grant_id() const noexcept { return grant_id_; }
    void retire() noexcept { invalidate(); }

private:
    void invalidate() noexcept;

    std::uint64_t grant_id_{0U};
    bool valid_{false};
};

struct ComplexSample final {
    float real{0.0F};
    float imag{0.0F};

    friend bool operator==(ComplexSample lhs, ComplexSample rhs) noexcept {
        return lhs.real == rhs.real && lhs.imag == rhs.imag;
    }
};

constexpr std::size_t kMaximumContractChunkSamples = 64U;

class AcquisitionChunkLease final {
public:
    AcquisitionChunkLease(
        const std::array<ComplexSample, kMaximumContractChunkSamples>& samples,
        std::size_t size) noexcept;
    AcquisitionChunkLease(AcquisitionChunkLease&& other) noexcept;
    AcquisitionChunkLease& operator=(AcquisitionChunkLease&& other) noexcept;
    AcquisitionChunkLease(const AcquisitionChunkLease&) = delete;
    AcquisitionChunkLease& operator=(const AcquisitionChunkLease&) = delete;

    bool valid() const noexcept { return valid_; }
    std::size_t size() const noexcept { return size_; }
    const ComplexSample& operator[](std::size_t index) const noexcept {
        return samples_[index];
    }

private:
    void invalidate() noexcept;

    std::array<ComplexSample, kMaximumContractChunkSamples> samples_{};
    std::size_t size_{0U};
    bool valid_{false};
};

enum class ReceiverWave {
    IncidentA,
    ResponseB
};

enum class ReceiverQualityFlag : std::uint32_t {
    Overload = 1U << 0U,
    ReceiverUnlocked = 1U << 1U,
    SourceUnleveled = 1U << 2U,
    TimebaseUnlocked = 1U << 3U
};

struct ChunkQuality final {
    std::uint32_t flags{0U};

    bool has(ReceiverQualityFlag flag) const noexcept {
        return (flags & static_cast<std::uint32_t>(flag)) != 0U;
    }
};

struct ReceiverObservationChunk final {
    ManifestId manifest_id{};
    PreparedExecutionId prepared_id{};
    BoardRunId run_id{};
    RunGeneration run_generation{};
    ChunkSequence sequence{};
    ReceiverWave wave{ReceiverWave::IncidentA};
    std::uint32_t point_begin{0U};
    AcquisitionChunkLease payload;
    ChunkQuality quality{};
};

enum class ChunkIngressDisposition {
    Accepted,
    AbortRunCapacityBreach,
    AbortRunProtocolViolation
};

enum class BoardRunPhase {
    Starting,
    Acquiring
};

struct BoardRunPhaseEvent final {
    BoardRunId run_id{};
    RunGeneration generation{};
    BoardRunPhase phase{BoardRunPhase::Starting};
};

enum class RunTerminalKind {
    Completed,
    Failed,
    Aborted
};

struct BoardRunTerminal final {
    BoardRunId run_id{};
    RunGeneration generation{};
    RunTerminalKind kind{RunTerminalKind::Failed};
    std::uint32_t delivered_chunks{0U};
};

class BoardRunSink {
public:
    virtual ~BoardRunSink() = default;
    virtual void on_phase(const BoardRunPhaseEvent& event) noexcept = 0;
    virtual ChunkIngressDisposition on_chunk(
        ReceiverObservationChunk&& chunk) noexcept = 0;
    virtual void on_terminal(BoardRunTerminal&& terminal) noexcept = 0;
};

class BoardRunSinkRegistration final {
public:
    explicit BoardRunSinkRegistration(BoardRunSink& sink) noexcept : sink_(&sink) {}
    BoardRunSinkRegistration(BoardRunSinkRegistration&& other) noexcept;
    BoardRunSinkRegistration& operator=(BoardRunSinkRegistration&& other) noexcept;
    BoardRunSinkRegistration(const BoardRunSinkRegistration&) = delete;
    BoardRunSinkRegistration& operator=(const BoardRunSinkRegistration&) = delete;

    bool valid() const noexcept { return sink_ != nullptr; }
    BoardRunSink& sink() const noexcept { return *sink_; }

private:
    BoardRunSink* sink_{nullptr};
};

struct RunAccepted final {
    BoardRunId run{};
    RunGeneration generation{};
};

struct ReclaimedRunInputs final {
    PreparedStartToken prepared;
    StartAuthorization authorization;
    RunDeliveryGrant delivery;
    BoardRunSinkRegistration sink;
};

struct RunRejected final {
    BoardError error{};
    ReclaimedRunInputs reclaimed;
};

using RunSubmission = std::variant<RunAccepted, RunRejected>;

class BoardExecutionPort {
public:
    virtual ~BoardExecutionPort() = default;
    virtual CapabilitySnapshot capabilities() const noexcept = 0;
    virtual PrepareSubmission begin_prepare(
        PrepareCallId call,
        SweepIntent intent,
        PrepareAuthorization&& authorization,
        PrepareSinkRegistration&& sink) noexcept = 0;
    virtual RunSubmission begin_run(
        BoardRunId run,
        RunGeneration generation,
        PreparedStartToken&& prepared,
        StartAuthorization&& authorization,
        RunDeliveryGrant&& delivery,
        BoardRunSinkRegistration&& sink) noexcept = 0;
};

class BoardSession {
public:
    virtual ~BoardSession() = default;
    virtual BoardExecutionPort& execution() noexcept = 0;
    virtual const CapabilitySnapshot& initial_capabilities() const noexcept = 0;
};

class OpenedBoard final {
public:
    explicit OpenedBoard(std::unique_ptr<BoardSession> owner) noexcept
        : owner_(std::move(owner)) {}
    OpenedBoard(OpenedBoard&&) noexcept = default;
    OpenedBoard& operator=(OpenedBoard&&) noexcept = default;
    OpenedBoard(const OpenedBoard&) = delete;
    OpenedBoard& operator=(const OpenedBoard&) = delete;

    BoardExecutionPort& execution() noexcept { return owner_->execution(); }
    const CapabilitySnapshot& initial_capabilities() const noexcept {
        return owner_->initial_capabilities();
    }

private:
    std::unique_ptr<BoardSession> owner_;
};

class BoardProvider {
public:
    virtual ~BoardProvider() = default;
    virtual core::Result<BoardInventorySnapshot, BoardError> discover(
        const BoardDiscoveryRequest& request) noexcept = 0;
    virtual core::Result<OpenedBoard, BoardError> open(
        const BoardOpenRequest& request) noexcept = 0;
};

}  // namespace vna::board
