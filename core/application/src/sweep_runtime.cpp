#include <vna/application/sweep_runtime.hpp>


#include "sweep_runtime_internal.hpp"

#include <stdexcept>
#include <utility>

namespace vna::application::internal {

SweepRuntimeImpl::SweepRuntimeImpl(
    SweepRuntimePlan plan,
    acquisition::RawSweepCaptureSource source,
    SweepPreviewExchange& previews,
    TracePublicationCatalog& catalog,
    OperationManager& operations)
    : plan_(std::move(plan)),
      source_(std::move(source)),
      previews_(previews),
      catalog_(catalog),
      operations_(operations) {
    if (!source_ || plan_.publication == nullptr ||
        plan_.maximumPointsPerChunk == 0 || plan_.execution.sweepCount == 0 ||
        plan_.execution.sweepCount > 100'000) {
        throw std::invalid_argument{"invalid sweep runtime plan"};
    }
    snapshot_.phase = plan_.execution.mode == domain::SweepMode::Single
        ? SweepRuntimePhase::Hold
        : SweepRuntimePhase::Preparing;
    snapshot_.configuredStateRevision = plan_.publication->stateRevision;
    snapshot_.configuredExecution = plan_.execution;
    snapshot_.appliedStateRevision = plan_.publication->stateRevision;
    snapshot_.appliedGeneration = plan_.publication->generation;
    snapshot_.appliedExecution = plan_.execution;
    worker_ = std::jthread{[this](std::stop_token token) { run(token); }};
}

SweepRuntimeImpl::~SweepRuntimeImpl() { stop(); }

void SweepRuntimeImpl::stop() noexcept {
    std::optional<OperationId> queued;
    std::optional<OperationId> activeWithoutSource;
    std::optional<OperationId> activeWithSource;
    std::shared_ptr<std::stop_source> activeStop;
    {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [&] { return !finalizingPublication_; });
        queued = std::exchange(pendingOperation_, std::nullopt);
        activeStop = activeStop_;
        cycleCancellationRequested_ = activeStop != nullptr;
        if (activeRequest_.has_value()) {
            if (activeStop) {
                activeWithSource = activeRequest_->operationId;
            } else {
                activeWithoutSource = activeRequest_->operationId;
                activeRequest_.reset();
            }
        }
    }
    auto invariant = cancelDetachedRequests(queued, activeWithoutSource);
    try {
        if (activeWithSource.has_value()) {
            requireTransition(
                operations_.requestCancel(*activeWithSource),
                "requestCancel");
        }
    } catch (...) {
        invariant = invariant == nullptr ? std::current_exception() : invariant;
    }
    if (activeStop) {
        activeStop->request_stop();
    }
    worker_.request_stop();
    notifyWorker();
    if (worker_.joinable()) {
        worker_.join();
    }
    if (invariant != nullptr) {
        failTerminal(invariant, queued, activeWithoutSource);
    }
}

void SweepRuntimeImpl::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

SweepRuntimeSnapshot SweepRuntimeImpl::snapshot() const {
    std::lock_guard lock{mutex_};
    return snapshot_;
}

void SweepRuntimeImpl::notifyWorker() const {
    std::lock_guard lock{mutex_};
    changed_.notify_all();
}

void SweepRuntimeImpl::recordAttempt() {
    std::lock_guard lock{mutex_};
    ++snapshot_.attemptedSweeps;
}

void SweepRuntimeImpl::recordCompleted() {
    std::lock_guard lock{mutex_};
    ++snapshot_.completedSweeps;
}

void SweepRuntimeImpl::reject(SweepRuntimeFailure value) {
    std::lock_guard lock{mutex_};
    ++snapshot_.rejectedSweeps;
    snapshot_.lastSweepFailure = std::move(value);
}

void SweepRuntimeImpl::finish(SweepRuntimeState state) noexcept {
    std::lock_guard lock{mutex_};
    if (snapshot_.state == SweepRuntimeState::Running) {
        snapshot_.state = state;
    }
}

}  // namespace vna::application::internal

namespace vna::application {

PreparedSweepRuntimeConfiguration::PreparedSweepRuntimeConfiguration(
    std::unique_ptr<detail::PreparedSweepRuntimeConfigurationState> state)
    : state_(std::move(state)) {}

PreparedSweepRuntimeConfiguration::PreparedSweepRuntimeConfiguration(
    PreparedSweepRuntimeConfiguration&&) noexcept = default;
PreparedSweepRuntimeConfiguration&
PreparedSweepRuntimeConfiguration::operator=(
    PreparedSweepRuntimeConfiguration&&) noexcept = default;
PreparedSweepRuntimeConfiguration::~PreparedSweepRuntimeConfiguration() =
    default;

class SweepRuntime::Impl final : public internal::SweepRuntimeImpl {
public:
    using SweepRuntimeImpl::SweepRuntimeImpl;
};

SweepRuntime::SweepRuntime(
    SweepRuntimePlan plan,
    acquisition::RawSweepCaptureSource source,
    SweepPreviewExchange& previews,
    TracePublicationCatalog& catalog,
    OperationManager& operations)
    : impl_(std::make_unique<Impl>(
          std::move(plan), std::move(source), previews, catalog, operations)) {}

SweepRuntime::~SweepRuntime() = default;
void SweepRuntime::stop() noexcept { impl_->stop(); }
void SweepRuntime::join() { impl_->join(); }
SweepRuntimeConfigurationPrepareResult SweepRuntime::prepareConfiguration(
    const StateSnapshot& candidate) {
    return impl_->prepareConfiguration(candidate);
}
void SweepRuntime::commitConfiguration(
    PreparedSweepRuntimeConfiguration prepared) noexcept {
    impl_->commitConfiguration(std::move(prepared));
}
SweepRuntimeRequestResult SweepRuntime::requestRestart(
    domain::ChannelId channelId,
    OperationSubmission submission) {
    return impl_->requestRestart(channelId, std::move(submission));
}
SweepRuntimeSnapshot SweepRuntime::snapshot() const { return impl_->snapshot(); }

}  // namespace vna::application
