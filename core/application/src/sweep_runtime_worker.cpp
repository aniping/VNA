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
        if (previewRejected) {
            return;
        }
        auto assembled = assembler.append(range);
        if (const auto* preview = std::get_if<SweepPreview>(&assembled)) {
            const auto published = previews_.publish(*preview);
            previewRejected =
                std::holds_alternative<SweepPreviewError>(published);
        } else if (std::holds_alternative<
                       SweepPreviewAssemblyError>(assembled)) {
            previewRejected = true;
        }
        if (previewRejected) {
            rejectPreview(identity);
        }
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

SweepDisposition SweepRuntimeImpl::complete(
    std::uint64_t sequence,
    SweepPreviewIdentity identity,
    acquisition::RawSweepCaptureResult captured) {
    if (const auto* error = std::get_if<frames::FrameError>(&captured)) {
        invalidate(identity);
        reject(failure(
            SweepRuntimeFailureCode::CaptureFailed, sequence, *error));
        return SweepDisposition::Continue;
    }
    auto raw = acquisition::RawFrame{
        .context = {acquisition::FrameId{sequence}, identity.sweepId,
                    sequence},
        .frequencyAxis = plan_.acquisition.frequencyAxis,
        .payload = std::get<frames::RawReceiverPayload>(std::move(captured)),
    };
    auto frameSet =
        internal::buildTraceDisplayFrameSet(raw, *plan_.publication);
    if (!frameSet) {
        invalidate(identity);
        reject(failure(
            SweepRuntimeFailureCode::CompleteProcessingFailed, sequence));
        return SweepDisposition::Continue;
    }
    const auto published = catalog_.publishIfCurrent(
        plan_.publication, std::move(*frameSet));
    invalidate(identity);
    if (const auto* error =
            std::get_if<TracePublicationCatalogError>(&published)) {
        if (error->code ==
            TracePublicationCatalogErrorCode::StalePublication) {
            return SweepDisposition::Retire;
        }
        reject(failure(
            SweepRuntimeFailureCode::PublicationRejected, sequence, *error));
        return SweepDisposition::Continue;
    }
    {
        std::lock_guard lock{mutex_};
        activeStop_.reset();
        activeIdentity_.reset();
    }
    recordCompleted();
    completeRequestedSweep(frames::FrameId{sequence});
    return SweepDisposition::Continue;
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

void SweepRuntimeImpl::failTerminal(std::exception_ptr failure) noexcept {
    std::lock_guard lock{mutex_};
    snapshot_.state = SweepRuntimeState::Failed;
    snapshot_.terminalFailure = std::move(failure);
}

}  // namespace vna::application::internal
