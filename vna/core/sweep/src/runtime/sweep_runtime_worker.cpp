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

void SweepRuntimeImpl::run(vna::compat::StopToken token) noexcept {
    std::uint64_t sequence = 1;
    try {
        while (!token.stopRequested()) {
            if (!prepareCycle(token)) {
                finish(SweepRuntimeState::Stopped);
                return;
            }
            const auto startedAt = std::chrono::steady_clock::now();
            recordAttempt();
            const auto disposition = capture(sequence, token);
            if (token.stopRequested()) {
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
        failTerminal(std::current_exception());
    }
}

SweepDisposition SweepRuntimeImpl::capture(
    std::uint64_t sequence,
    vna::compat::StopToken token) {
    const auto identity = SweepPreviewIdentity{
        plan_.publication->generation, acquisition::SweepId{sequence}};
    std::shared_ptr<vna::compat::StopSource> cycleStop;
    {
        std::lock_guard lock{mutex_};
        activeIdentity_ = identity;
        cycleStop = activeStop_;
        setDisplayStatusLocked(
            SweepUserPhase::Preparing,
            identity.sweepId,
            0);
        previews_.updateForRuntime(displayStatusLocked());
    }
    vna::compat::StopCallback stopCycle{
        token, [cycleStop] { cycleStop->requestStop(); }};
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
    auto captured = source_(
        {plan_.acquisition, identity.sweepId, sequence,
         plan_.maximumPointsPerChunk},
        observer, cycleStop->getToken());
    if (cycleStop->getToken().stopRequested()) {
        cancelActiveAfterSource(identity);
        return SweepDisposition::Canceled;
    }
    if (std::holds_alternative<
            acquisition::RawSweepCaptureCanceled>(captured)) {
        throw std::logic_error{"capture canceled without a stop request"};
    }
    if (std::holds_alternative<frames::RawReceiverPayload>(captured)) {
        std::lock_guard lock{mutex_};
        setDisplayStatusLocked(
            SweepUserPhase::Calculation,
            identity.sweepId,
            snapshot_.progress.totalPoints);
        previews_.updateForRuntime(displayStatusLocked());
    }
    return complete(sequence, identity, std::move(captured));
}

SweepDisposition SweepRuntimeImpl::complete(
    std::uint64_t sequence, SweepPreviewIdentity identity,
    acquisition::RawSweepCaptureResult captured) {
    if (const auto* error = std::get_if<frames::FrameError>(&captured)) {
        const auto rejected = failure(
            SweepRuntimeFailureCode::CaptureFailed, sequence, *error);
        reject(identity, rejected);
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
        const auto rejected = failure(
            SweepRuntimeFailureCode::CompleteProcessingFailed, sequence);
        reject(identity, rejected);
        failRequestedSweep(rejected);
        return SweepDisposition::Continue;
    }
    if (!claimPublication()) {
        cancelActiveAfterSource(identity);
        return SweepDisposition::Canceled;
    }
    const auto published = catalog_.publishIfCurrent(
        plan_.publication, std::move(*frameSet));
    if (const auto* error =
            std::get_if<TracePublicationCatalogError>(&published)) {
        if (error->code ==
            TracePublicationCatalogErrorCode::StalePublication) {
            retireAfterSource(identity);
            return SweepDisposition::Retire;
        }
        const auto rejected = failure(
            SweepRuntimeFailureCode::PublicationRejected, sequence, *error);
        reject(identity, rejected);
        failRequestedSweep(rejected);
        return SweepDisposition::Continue;
    }
    completeRequestedSweep(identity, frames::FrameId{sequence});
    return SweepDisposition::Continue;
}

bool SweepRuntimeImpl::claimPublication() noexcept {
    std::lock_guard lock{mutex_};
    if (cycleCancellationRequested_) {
        return false;
    }
    // A publication that already claimed this gate may finish; a replacement
    // starts at the next boundary without waiting in CommandBus admission.
    finalizingPublication_ = true;
    return true;
}

bool SweepRuntimeImpl::paceUntil(
    std::chrono::steady_clock::time_point deadline,
    vna::compat::StopToken token) const {
    std::unique_lock lock{mutex_};
    changed_.wait_until(lock, deadline, [&] {
        return token.stopRequested();
    });
    return !token.stopRequested();
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
        identity = activeIdentity_;
        setDisplayStatusLocked(
            SweepUserPhase::Failed,
            identity.has_value()
                ? std::optional{identity->sweepId}
                : snapshot_.activeSweepId,
            snapshot_.progress.completedPoints);
        if (identity.has_value()) {
            invalidateLocked(*identity);
        } else {
            previews_.updateForRuntime(displayStatusLocked());
        }
        activeIdentity_.reset();
        activeStop_.reset();
        if (activeRequest_.has_value()) {
            active = activeRequest_->operationId;
            activeRequest_.reset();
        }
        changed_.notify_all();
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
