#include <vna/application/sweep_runtime.hpp>

#include "continuous_trace_pipeline_internal.hpp"

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <vna/application/sweep_preview_assembler.hpp>
namespace vna::application {
namespace {
enum class SweepDisposition { Continue, Retire };

SweepRuntimeFailure failure(
    SweepRuntimeFailureCode code,
    std::uint64_t sequence,
    SweepRuntimeFailureCause cause = {}) {
    return {code, sequence, std::move(cause)};
}

}  // namespace
class SweepRuntime::Impl {
public:
    Impl(
        SweepRuntimePlan plan,
        acquisition::RawSweepCaptureSource source,
        SweepPreviewExchange& previews,
        TracePublicationCatalog& catalog)
        : plan_(std::move(plan)),
          source_(std::move(source)),
          previews_(previews),
          catalog_(catalog) {
        if (!source_ || plan_.publication == nullptr ||
            plan_.maximumPointsPerChunk == 0) {
            throw std::invalid_argument{"invalid sweep runtime plan"};
        }
        worker_ = std::jthread{[this](std::stop_token token) { run(token); }};
    }

    ~Impl() { stop(); }
    void stop() noexcept {
        worker_.request_stop();
        notifyWorker();
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    void join() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    SweepRuntimeSnapshot snapshot() const {
        std::lock_guard lock{mutex_};
        return snapshot_;
    }
private:
    void run(std::stop_token token) noexcept {
        std::uint64_t sequence = 1;
        try {
            while (!token.stop_requested()) {
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

    SweepDisposition capture(
        std::uint64_t sequence,
        std::stop_token token) {
        const auto identity = SweepPreviewIdentity{
            plan_.publication->generation, acquisition::SweepId{sequence}};
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
        auto captured = source_(
            {plan_.acquisition, identity.sweepId, sequence,
             plan_.maximumPointsPerChunk},
            observer, token);
        if (token.stop_requested()) {
            invalidate(identity);
            return SweepDisposition::Continue;
        }
        if (std::holds_alternative<
                acquisition::RawSweepCaptureCanceled>(captured)) {
            throw std::logic_error{"capture canceled without a stop request"};
        }
        return complete(sequence, identity, std::move(captured));
    }

    SweepDisposition complete(
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
            .payload = std::get<frames::RawReceiverPayload>(
                std::move(captured)),
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
                SweepRuntimeFailureCode::PublicationRejected,
                sequence, *error));
            return SweepDisposition::Continue;
        }
        recordCompleted();
        return SweepDisposition::Continue;
    }

    bool paceUntil(
        std::chrono::steady_clock::time_point deadline,
        std::stop_token token) const {
        std::unique_lock lock{mutex_};
        changed_.wait_until(lock, deadline, [&] {
            return token.stop_requested();
        });
        return !token.stop_requested();
    }

    void notifyWorker() const {
        std::lock_guard lock{mutex_};
        changed_.notify_all();
    }

    void recordAttempt() {
        std::lock_guard lock{mutex_};
        ++snapshot_.attemptedSweeps;
    }
    void recordCompleted() {
        std::lock_guard lock{mutex_};
        ++snapshot_.completedSweeps;
    }
    void reject(SweepRuntimeFailure value) {
        std::lock_guard lock{mutex_};
        ++snapshot_.rejectedSweeps;
        snapshot_.lastSweepFailure = std::move(value);
    }

    void rejectPreview(SweepPreviewIdentity identity) noexcept {
        invalidate(identity);
        std::lock_guard lock{mutex_};
        ++snapshot_.previewRejectedSweeps;
    }

    void invalidate(SweepPreviewIdentity identity) noexcept {
        static_cast<void>(previews_.invalidate(identity));
    }
    void finish(SweepRuntimeState state) noexcept {
        std::lock_guard lock{mutex_};
        snapshot_.state = state;
    }
    void failTerminal(std::exception_ptr failure) noexcept {
        std::lock_guard lock{mutex_};
        snapshot_.state = SweepRuntimeState::Failed;
        snapshot_.terminalFailure = std::move(failure);
    }

    const SweepRuntimePlan plan_;
    const acquisition::RawSweepCaptureSource source_;
    SweepPreviewExchange& previews_;
    TracePublicationCatalog& catalog_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    SweepRuntimeSnapshot snapshot_;
    std::jthread worker_;
};

SweepRuntime::SweepRuntime(
    SweepRuntimePlan plan,
    acquisition::RawSweepCaptureSource source,
    SweepPreviewExchange& previews,
    TracePublicationCatalog& catalog)
    : impl_(std::make_unique<Impl>(
          std::move(plan), std::move(source), previews, catalog)) {}

SweepRuntime::~SweepRuntime() = default;
void SweepRuntime::stop() noexcept { impl_->stop(); }
void SweepRuntime::join() { impl_->join(); }
SweepRuntimeSnapshot SweepRuntime::snapshot() const { return impl_->snapshot(); }

}  // namespace vna::application
