#include "sweep_generation_transaction_internal.hpp"

#include <limits>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace vna::application::internal {
namespace {

using TraceMap = std::unordered_map<
    std::uint64_t, TraceDisplayFrameHandle>;
using PreviewEvent = std::optional<SweepPreviewEvent>;

static_assert(noexcept(std::declval<TraceMap&>().swap(
    std::declval<TraceMap&>())));
static_assert(std::is_nothrow_swappable_v<PreviewEvent>);
static_assert(std::is_nothrow_swappable_v<TracePublicationPlanHandle>);
static_assert(std::is_nothrow_move_assignable_v<SweepPreviewHandle>);
static_assert(std::is_nothrow_move_assignable_v<TraceDisplayFrameSetHandle>);

}  // namespace

SweepGenerationCommitResult SweepGenerationTransaction::commit(
    TracePublicationCatalog& catalog,
    SweepPreviewExchange& previews,
    PreparedTracePublicationPlan& prepared) {
    std::unique_lock catalogLock{catalog.mutex_};
    if (prepared.basePlan_.get() != catalog.current_.get()) {
        return SweepGenerationCommitError::StalePrepared;
    }
    auto candidate = prepared.candidate_;
    if (candidate->generation == catalog.current_->generation) {
        catalog.current_.swap(candidate);
        return catalog.current_;
    }
    return advance(catalog, previews, catalogLock, std::move(candidate));
}

bool SweepGenerationTransaction::canAdvance(
    const TraceDisplayFrameRepository& repository,
    const SweepPreviewExchange& previews,
    std::uint64_t current,
    std::uint64_t next) noexcept {
    auto valid = current != std::numeric_limits<std::uint64_t>::max() &&
        next == current + 1 && repository.generation_ == current &&
        previews.generation_ == current &&
        previews.nextCursor_ != std::numeric_limits<std::uint64_t>::max();
    for (const auto& [traceId, state] : repository.waitStates_) {
        static_cast<void>(traceId);
        valid = valid && state->discardGeneration !=
            std::numeric_limits<std::uint64_t>::max();
    }
    return valid;
}

SweepGenerationCommitResult SweepGenerationTransaction::advance(
    TracePublicationCatalog& catalog,
    SweepPreviewExchange& previews,
    std::unique_lock<std::mutex>& catalogLock,
    TracePublicationPlanHandle candidate) {
    auto& repository = catalog.repository_;
    std::unique_lock repositoryLock{repository.mutex_};
    std::unique_lock previewLock{previews.mutex_};
    if (!canAdvance(repository, previews, catalog.current_->generation,
                    candidate->generation)) {
        return SweepGenerationCommitError::GenerationMismatch;
    }
    TraceMap emptyFrames;
    PreviewEvent nextEvent{SweepPreviewGenerationAdvanced{
        SweepPreviewCursor{previews.nextCursor_}, candidate->generation}};
    const auto commitPrepared = [&]() noexcept {
        repository.generation_ = candidate->generation;
        repository.latestFrameSet_.reset();
        repository.latestByTrace_.swap(emptyFrames);
        for (const auto& [traceId, state] : repository.waitStates_) {
            static_cast<void>(traceId);
            ++state->discardGeneration;
            state->changed.notify_all();
        }
        previews.generation_ = candidate->generation;
        previews.activeIdentity_.reset();
        previews.currentPreview_.reset();
        ++previews.nextCursor_;
        previews.latestEvent_.swap(nextEvent);
        catalog.current_.swap(candidate);
        repository.frameSetChanged_.notify_all();
        previews.changed_.notify_all();
    };
    static_assert(noexcept(std::declval<std::condition_variable&>().notify_all()));
    static_assert(noexcept(commitPrepared()));
    commitPrepared();
    const auto committed = catalog.current_;
    previewLock.unlock();
    repositoryLock.unlock();
    catalogLock.unlock();
    return committed;
}

}  // namespace vna::application::internal
