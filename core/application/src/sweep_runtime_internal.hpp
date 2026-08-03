#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include <vna/application/sweep_runtime.hpp>
#include <vna/application/sweep_preview_assembler.hpp>

namespace vna::application::internal {

enum class SweepDisposition { Continue, Canceled, Retire };

struct ActiveSweepRequest {
    OperationId operationId;
    std::uint32_t remainingSweeps;
};

struct RestartAdmission {
    OperationId createdId;
    std::optional<OperationId> queued{};
    std::optional<OperationId> activeWithoutSource{};
    std::shared_ptr<std::stop_source> activeStop{};
    std::optional<SweepPreviewIdentity> activeIdentity{};
    std::exception_ptr invariant{};
};

struct PendingSweepRuntimeConfiguration {
    SweepRuntimePlan plan;
    PreparedTracePublicationPlan publication;
};

using RestartAdmissionResult = std::variant<RestartAdmission, SweepRuntimeRequestError>;

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
    [[nodiscard]] SweepRuntimeRequestResult requestRestart(
        domain::ChannelId channelId, OperationSubmission submission);
    [[nodiscard]] SweepRuntimeSnapshot snapshot() const;
private:
    [[nodiscard]] bool prepareCycle(std::stop_token token);
    void applyPendingConfiguration();
    void completeRequestedSweep(frames::FrameId frameId);
    void failRequestedSweep(const SweepRuntimeFailure& failure);
    void cancelActiveAfterSource();
    void retireAfterSource() noexcept;
    [[nodiscard]] std::exception_ptr cancelDetachedRequests(
        std::optional<OperationId> queued, std::optional<OperationId> active) noexcept;
    [[nodiscard]] RestartAdmissionResult admitRestart(
        domain::ChannelId channelId, OperationSubmission submission);
    void requireTransition(OperationResult result, const char* transition);
    void settleTerminalFailure(OperationId operationId) noexcept;
    void observePreviewRange(
        SweepPreviewAssembler& assembler,
        bool& previewRejected,
        SweepPreviewIdentity identity,
        const acquisition::RawSweepPointRange& range);
    void run(std::stop_token token) noexcept;
    [[nodiscard]] SweepDisposition capture(
        std::uint64_t sequence, std::stop_token token);
    [[nodiscard]] SweepDisposition complete(
        std::uint64_t sequence,
        SweepPreviewIdentity identity,
        acquisition::RawSweepCaptureResult captured);
    [[nodiscard]] bool paceUntil(
        std::chrono::steady_clock::time_point deadline,
        std::stop_token token) const;
    void notifyWorker() const;
    void recordAttempt();
    void recordCompleted();
    void reject(SweepRuntimeFailure failure);
    void rejectPreview(SweepPreviewIdentity identity) noexcept;
    [[nodiscard]] bool claimPublication() noexcept;
    void invalidate(SweepPreviewIdentity identity) noexcept;
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
    std::shared_ptr<std::stop_source> activeStop_;
    std::optional<SweepPreviewIdentity> activeIdentity_;
    std::unique_ptr<PendingSweepRuntimeConfiguration> pendingConfiguration_;
    bool finalizingPublication_{};
    bool cycleCancellationRequested_{};
    bool admissionClosed_{};
    std::jthread worker_;
};

}  // namespace vna::application::internal

namespace vna::application::detail {

struct PreparedSweepRuntimeConfigurationState {
    internal::SweepRuntimeImpl* owner;
    std::unique_lock<std::mutex> gate;
    std::unique_ptr<internal::PendingSweepRuntimeConfiguration> pending;
};

}  // namespace vna::application::detail
