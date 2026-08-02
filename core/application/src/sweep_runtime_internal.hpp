#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include <vna/application/sweep_runtime.hpp>

namespace vna::application::internal {

enum class SweepDisposition { Continue, Canceled, Retire };

struct ActiveSweepRequest {
    OperationId operationId;
    std::uint32_t remainingSweeps;
};

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
    [[nodiscard]] SweepRuntimeRequestResult requestRestart(
        OperationSubmission submission);
    [[nodiscard]] SweepRuntimeSnapshot snapshot() const;
private:
    [[nodiscard]] bool prepareCycle(std::stop_token token);
    void completeRequestedSweep(frames::FrameId frameId);
    void cancelActiveAfterSource();
    void cancelWithoutSource(OperationId operationId);
    void run(std::stop_token token) noexcept;
    [[nodiscard]] SweepDisposition capture(
        std::uint64_t sequence,
        std::stop_token token);
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
    void invalidate(SweepPreviewIdentity identity) noexcept;
    void finish(SweepRuntimeState state) noexcept;
    void failTerminal(std::exception_ptr failure) noexcept;

    const SweepRuntimePlan plan_;
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
    std::jthread worker_;
};

}  // namespace vna::application::internal
