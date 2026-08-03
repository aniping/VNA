#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <vna/compat/joining_thread.hpp>

#include <vna/application/sweep_runtime.hpp>
#include <vna/application/sweep_preview_assembler.hpp>

namespace vna::application::internal {

enum class SweepDisposition { Continue, Canceled, Retire };

struct ActiveSweepRequest {
    OperationId operationId;
    std::uint32_t remainingSweeps;
};

struct RestartAdmissionData {
    OperationId createdId;
    std::optional<OperationId> queued{};
    std::optional<OperationId> activeWithoutSource{};
    std::shared_ptr<vna::compat::StopSource> activeStop{};
    std::optional<SweepPreviewIdentity> activeIdentity{};
    std::exception_ptr invariant{};
};

struct PendingSweepRuntimeConfiguration {
    SweepRuntimePlan plan;
    PreparedTracePublicationPlan publication;
};

using RestartAdmissionResult =
    std::variant<RestartAdmissionData, SweepRuntimeRequestError>;

// The public runtime stays a narrow lifecycle seam. Its worker details live in
// this private type so control can grow without bloating its public facade.
class SweepRuntimeImpl {
public:
    SweepRuntimeImpl(
        SweepRuntimePlan plan,
        acquisition::RawSweepCaptureSource source,
        SweepPreviewExchange& previews,
        TracePublicationCatalog& catalog,
        OperationManager& operations);
    ~SweepRuntimeImpl();

    void stop() noexcept;
    void join();
    [[nodiscard]] SweepRuntimeConfigurationPrepareResult prepareConfiguration(
        const StateSnapshot& candidate);
    void commitConfiguration(
        PreparedSweepRuntimeConfiguration prepared) noexcept;
    [[nodiscard]] SweepRuntimeAdmissionResult admitRestart(
        domain::ChannelId channelId, OperationSubmission submission);
    [[nodiscard]] SweepRuntimeSnapshot snapshot() const;
private:
    [[nodiscard]] bool prepareCycle(vna::compat::StopToken token);
    void applyPendingConfiguration();
    void completeRequestedSweep(
        SweepPreviewIdentity identity,
        frames::FrameId frameId);
    void failRequestedSweep(const SweepRuntimeFailure& failure);
    void cancelActiveAfterSource(SweepPreviewIdentity identity);
    void retireAfterSource(SweepPreviewIdentity identity) noexcept;
    [[nodiscard]] std::exception_ptr cancelDetachedRequests(
        std::optional<OperationId> queued, std::optional<OperationId> active) noexcept;
    [[nodiscard]] RestartAdmissionResult prepareRestart(
        domain::ChannelId channelId, OperationSubmission submission);
    void settleRestart(RestartAdmissionData admission) noexcept;
    void requireTransition(OperationResult result, const char* transition);
    void settleTerminalFailure(OperationId operationId) noexcept;
    void observePreviewRange(
        SweepPreviewAssembler& assembler,
        bool& previewRejected,
        SweepPreviewIdentity identity,
        const acquisition::RawSweepPointRange& range);
    void run(vna::compat::StopToken token) noexcept;
    [[nodiscard]] SweepDisposition capture(
        std::uint64_t sequence, vna::compat::StopToken token);
    [[nodiscard]] SweepDisposition complete(
        std::uint64_t sequence,
        SweepPreviewIdentity identity,
        acquisition::RawSweepCaptureResult captured);
    [[nodiscard]] bool paceUntil(
        std::chrono::steady_clock::time_point deadline,
        vna::compat::StopToken token) const;
    void notifyWorker() const;
    void recordAttempt();
    void reject(
        SweepPreviewIdentity identity,
        SweepRuntimeFailure failure);
    [[nodiscard]] bool claimPublication() noexcept;
    [[nodiscard]] SweepRuntimeDisplayStatus displayStatusLocked() const;
    void setDisplayStatusLocked(
        SweepUserPhase phase,
        std::optional<acquisition::SweepId> sweepId,
        std::uint64_t completedPoints) noexcept;
    void invalidateLocked(SweepPreviewIdentity identity) noexcept;
    void finish(SweepRuntimeState state) noexcept;
    void failTerminal(
        std::exception_ptr failure,
        std::optional<OperationId> detachedFirst = std::nullopt,
        std::optional<OperationId> detachedSecond = std::nullopt) noexcept;

    SweepRuntimePlan plan_;
    const acquisition::RawSweepCaptureSource source_;
    SweepPreviewExchange& previews_;
    TracePublicationCatalog& catalog_;
    OperationManager& operations_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    SweepRuntimeSnapshot snapshot_;
    std::optional<OperationId> pendingOperation_;
    std::optional<ActiveSweepRequest> activeRequest_;
    std::shared_ptr<vna::compat::StopSource> activeStop_;
    std::optional<SweepPreviewIdentity> activeIdentity_;
    std::unique_ptr<PendingSweepRuntimeConfiguration> pendingConfiguration_;
    bool finalizingPublication_{};
    bool cycleCancellationRequested_{};
    bool admissionClosed_{};
    vna::compat::JoiningThread worker_;

    friend class vna::application::RestartAdmission;
};

}  // namespace vna::application::internal

namespace vna::application::detail {

struct PreparedSweepRuntimeConfigurationState {
    internal::SweepRuntimeImpl* owner;
    std::unique_lock<std::mutex> gate;
    std::unique_ptr<internal::PendingSweepRuntimeConfiguration> pending;
};

struct RestartAdmissionState {
    internal::SweepRuntimeImpl* owner{};
    std::optional<internal::RestartAdmissionData> admission;
};

}  // namespace vna::application::detail
