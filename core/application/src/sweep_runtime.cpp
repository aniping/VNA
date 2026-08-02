#include <vna/application/sweep_runtime.hpp>

#include "sweep_runtime_internal.hpp"

#include <stdexcept>
#include <utility>

namespace vna::application::internal {

SweepRuntimeImpl::SweepRuntimeImpl(
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

SweepRuntimeImpl::~SweepRuntimeImpl() { stop(); }

void SweepRuntimeImpl::stop() noexcept {
    worker_.request_stop();
    notifyWorker();
    if (worker_.joinable()) {
        worker_.join();
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
    snapshot_.state = state;
}

}  // namespace vna::application::internal

namespace vna::application {

class SweepRuntime::Impl final : public internal::SweepRuntimeImpl {
public:
    using SweepRuntimeImpl::SweepRuntimeImpl;
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
