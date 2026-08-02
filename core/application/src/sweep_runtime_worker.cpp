#include "sweep_runtime_internal.hpp"

#include "continuous_trace_pipeline_internal.hpp"

#include <stdexcept>
#include <utility>

#include <vna/application/sweep_preview_assembler.hpp>
namespace vna::application::internal {
namespace {

SweepRuntimeFailure failure(
    SweepRuntimeFailureCode code,
    std::uint64_t sequence,
    SweepRuntimeFailureCause cause = {}) {
    return {code, sequence, std::move(cause)};
}

}  // namespace

void SweepRuntimeImpl::run(std::stop_token token) noexcept {
    std::uint64_t sequence = 1;
    try {
        while (!token.stop_requested()) {
            if (!prepareCycle(token)) {
                finish(SweepRuntimeState::Stopped);
                return;
            }
            const auto startedAt = std::chrono::steady_clock::now();
            recordAttempt();
            const auto disposition = capture(sequence, token);
            if (token.stop_requested()) {
                finish(SweepRuntimeState::Stopped);
                return;
            }
            if (disposition == SweepDisposition::Retire) {
                finish(SweepRuntimeState::Retired);
                return;
            }
            ++sequence;
            if (disposition == SweepDisposition::Canceled) {
                continue;
            }
            if (!paceUntil(
                    startedAt + plan_.acquisition.minimumSweepPeriod,
                    token)) {
                finish(SweepRuntimeState::Stopped);
                return;
            }
        }
        finish(SweepRuntimeState::Stopped);
    } catch (...) {
        invalidate({plan_.publication->generation,
                    acquisition::SweepId{sequence}});
        failTerminal(std::current_exception());
    }
}

SweepDisposition SweepRuntimeImpl::capture(
    std::uint64_t sequence,
    std::stop_token token) {
    const auto identity = SweepPreviewIdentity{
        plan_.publication->generation, acquisition::SweepId{sequence}};
    std::shared_ptr<std::stop_source> cycleStop;
    {
        std::lock_guard lock{mutex_};
        activeIdentity_ = identity;
        cycleStop = activeStop_;
    }
    std::stop_callback stopCycle{
        token, [cycleStop] { cycleStop->request_stop(); }};
    SweepPreviewAssembler assembler{{
        plan_.acquisition,
        plan_.publication,
        identity.sweepId,
        sequence,
    }};
    bool previewRejected = false;
    const auto observer = [&](const auto& range) {
        observePreviewRange(assembler, previewRejected, identity, range);
    };
    {
        std::lock_guard lock{mutex_};
        snapshot_.phase = SweepRuntimePhase::Sweeping;
    }
    auto captured = source_(
        {plan_.acquisition, identity.sweepId, sequence,
         plan_.maximumPointsPerChunk},
        observer, cycleStop->get_token());
    if (cycleStop->stop_requested()) {
        invalidate(identity);
        cancelActiveAfterSource();
        return SweepDisposition::Canceled;
    }
    if (std::holds_alternative<
            acquisition::RawSweepCaptureCanceled>(captured)) {
        throw std::logic_error{"capture canceled without a stop request"};
    }
    {
        std::lock_guard lock{mutex_};
        snapshot_.phase = SweepRuntimePhase::Publishing;
    }
    return complete(sequence, identity, std::move(captured));
}

void SweepRuntimeImpl::observePreviewRange(
    SweepPreviewAssembler& assembler,
    bool& previewRejected,
    SweepPreviewIdentity identity,
    const acquisition::RawSweepPointRange& range) {
    if (previewRejected) {
        return;
    }
    auto assembled = assembler.append(range);
    if (const auto* preview = std::get_if<SweepPreview>(&assembled)) {
        const auto published = previews_.publish(*preview);
        previewRejected =
            std::holds_alternative<SweepPreviewError>(published);
    } else if (std::holds_alternative<SweepPreviewAssemblyError>(assembled)) {
        previewRejected = true;
    }
    if (previewRejected) {
        rejectPreview(identity);
    }
}

SweepDisposition SweepRuntimeImpl::complete(
    std::uint64_t sequence, SweepPreviewIdentity identity,
    acquisition::RawSweepCaptureResult captured) {
    if (const auto* error = std::get_if<frames::FrameError>(&captured)) {
        invalidate(identity);
        const auto rejected = failure(
            SweepRuntimeFailureCode::CaptureFailed, sequence, *error);
        reject(rejected);
        failRequestedSweep(rejected);
        return SweepDisposition::Continue;
    }
    auto raw = acquisition::RawFrame{
        .context = {acquisition::FrameId{sequence}, identity.sweepId, sequence},
        .frequencyAxis = plan_.acquisition.frequencyAxis,
        .payload = std::get<frames::RawReceiverPayload>(std::move(captured)),
    };
    auto frameSet = internal::buildTraceDisplayFrameSet(raw, *plan_.publication);
    if (!frameSet) {
        invalidate(identity);
        const auto rejected = failure(
            SweepRuntimeFailureCode::CompleteProcessingFailed, sequence);
        reject(rejected);
        failRequestedSweep(rejected);
        return SweepDisposition::Continue;
    }
    if (!claimPublication()) {
        invalidate(identity);
        cancelActiveAfterSource();
        return SweepDisposition::Canceled;
    }
    const auto published = catalog_.publishIfCurrent(
        plan_.publication, std::move(*frameSet));
    invalidate(identity);
    if (const auto* error =
            std::get_if<TracePublicationCatalogError>(&published)) {
        if (error->code ==
            TracePublicationCatalogErrorCode::StalePublication) {
            retireAfterSource();
            return SweepDisposition::Retire;
        }
        const auto rejected = failure(
            SweepRuntimeFailureCode::PublicationRejected, sequence, *error);
        reject(rejected);
        failRequestedSweep(rejected);
        return SweepDisposition::Continue;
    }
    recordCompleted();
    completeRequestedSweep(frames::FrameId{sequence});
    return SweepDisposition::Continue;
}

bool SweepRuntimeImpl::claimPublication() noexcept {
    std::lock_guard lock{mutex_};
    if (cycleCancellationRequested_) {
        return false;
    }
    // Restart admission waits for this short external publication gate. The
    // operation callback runs only after the gate is released.
    finalizingPublication_ = true;
    return true;
}

bool SweepRuntimeImpl::paceUntil(
    std::chrono::steady_clock::time_point deadline,
    std::stop_token token) const {
    std::unique_lock lock{mutex_};
    changed_.wait_until(lock, deadline, [&] {
        return token.stop_requested();
    });
    return !token.stop_requested();
}

void SweepRuntimeImpl::rejectPreview(
    SweepPreviewIdentity identity) noexcept {
    invalidate(identity);
    std::lock_guard lock{mutex_};
    ++snapshot_.previewRejectedSweeps;
}

void SweepRuntimeImpl::invalidate(SweepPreviewIdentity identity) noexcept {
    static_cast<void>(previews_.invalidate(identity));
}

void SweepRuntimeImpl::failTerminal(
    std::exception_ptr failure,
    std::optional<OperationId> detachedFirst,
    std::optional<OperationId> detachedSecond) noexcept {
    std::optional<OperationId> queued;
    std::optional<OperationId> active;
    std::optional<SweepPreviewIdentity> identity;
    {
        std::lock_guard lock{mutex_};
        admissionClosed_ = true;
        finalizingPublication_ = false;
        cycleCancellationRequested_ = false;
        queued = std::exchange(pendingOperation_, std::nullopt);
        identity = std::exchange(activeIdentity_, std::nullopt);
        activeStop_.reset();
        if (activeRequest_.has_value()) {
            active = activeRequest_->operationId;
            activeRequest_.reset();
        }
        changed_.notify_all();
    }
    if (identity.has_value()) {
        invalidate(*identity);
    }
    for (const auto operation : {
             detachedFirst, detachedSecond, queued, active}) {
        if (operation.has_value()) {
            settleTerminalFailure(*operation);
        }
    }
    std::lock_guard lock{mutex_};
    snapshot_.state = SweepRuntimeState::Failed;
    snapshot_.terminalFailure = std::move(failure);
    changed_.notify_all();
}
}  // namespace vna::application::internal
