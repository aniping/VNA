#include <vna/application/sweep_preview_exchange.hpp>

#include "sweep_preview_validation_internal.hpp"

#include <limits>
#include <type_traits>
#include <utility>

namespace vna::application {
namespace {

using PreviewEvent = std::optional<SweepPreviewEvent>;

static_assert(std::is_nothrow_swappable_v<PreviewEvent>);
static_assert(std::is_nothrow_move_assignable_v<SweepPreviewHandle>);

SweepPreviewCursor cursorOf(const SweepPreviewEvent& event) {
    return std::visit(
        [](const auto& value) { return value.cursor; }, event);
}

bool statusMatchesPreview(
    const SweepRuntimeDisplayStatus& status,
    const SweepPreview& preview) {
    return status.generation == preview.identity.generation &&
        status.channelId == preview.channelId &&
        status.stateRevision == preview.stateRevision &&
        status.sweepId == std::optional{preview.identity.sweepId};
}

}  // namespace

SweepPreviewPublishResult SweepPreviewExchange::publish(
    SweepPreview preview) {
    return publishImpl(std::move(preview), nullptr);
}

SweepPreviewPublishResult SweepPreviewExchange::publishForRuntime(
    SweepPreview preview,
    SweepRuntimeDisplayStatus status) {
    if (!validStatus(status)) {
        return SweepPreviewError{SweepPreviewErrorCode::InvalidIdentity};
    }
    return publishImpl(std::move(preview), &status);
}

SweepPreviewPublishResult SweepPreviewExchange::publishImpl(
    SweepPreview preview,
    const SweepRuntimeDisplayStatus* runtimeStatus) {
    if (const auto invalid = internal::validateSweepPreview(preview)) {
        return *invalid;
    }
    SweepPreviewHandle handle;
    {
        std::lock_guard lock{mutex_};
        if (preview.identity.generation < generation_) {
            return SweepPreviewError{SweepPreviewErrorCode::StaleGeneration};
        }
        if (preview.identity.generation > generation_) {
            return SweepPreviewError{SweepPreviewErrorCode::FutureGeneration};
        }
        const auto sweepId = preview.identity.sweepId.value();
        const auto continuing = activeIdentity_.has_value() &&
            *activeIdentity_ == preview.identity;
        if (continuing && currentPreview_ != nullptr &&
            !internal::isCumulativeExtension(*currentPreview_, preview)) {
            return SweepPreviewError{SweepPreviewErrorCode::ProgressRegression};
        }
        if (!continuing && sweepId <= lastSweepId_) {
            return SweepPreviewError{SweepPreviewErrorCode::SweepIdRegression};
        }
        if (runtimeStatus != nullptr &&
            !statusMatchesPreview(*runtimeStatus, preview)) {
            return SweepPreviewError{SweepPreviewErrorCode::InvalidIdentity};
        }
        handle = std::make_shared<const SweepPreview>(std::move(preview));
        const auto nextStatus = runtimeStatus == nullptr
            ? status_
            : *runtimeStatus;
        PreviewEvent nextEvent{SweepPreviewAvailable{
            SweepPreviewCursor{nextCursor_}, handle,
            streamStatus(nextStatus, handle)}};
        if (!continuing) {
            activeIdentity_ = handle->identity;
            lastSweepId_ = sweepId;
        }
        status_ = nextStatus;
        currentPreview_ = handle;
        ++nextCursor_;
        latestEvent_.swap(nextEvent);
    }
    changed_.notify_all();
    return handle;
}

SweepPreviewGenerationResult SweepPreviewExchange::advanceGeneration(
    std::uint64_t nextGeneration) {
    std::lock_guard lock{mutex_};
    if (generation_ == std::numeric_limits<std::uint64_t>::max() ||
        nextCursor_ == std::numeric_limits<std::uint64_t>::max() ||
        nextGeneration != generation_ + 1) {
        return SweepPreviewError{SweepPreviewErrorCode::GenerationNotNext};
    }
    auto nextStatus = status_;
    nextStatus.generation = nextGeneration;
    nextStatus.sweepId.reset();
    nextStatus.userPhase = status_.userPhase == SweepUserPhase::Hold
        ? SweepUserPhase::Hold
        : SweepUserPhase::Preparing;
    nextStatus.progress.completedPoints =
        nextStatus.userPhase == SweepUserPhase::Hold
        ? nextStatus.progress.totalPoints
        : 0;
    nextStatus.firstSweepAfterConfiguration = true;
    SweepPreviewGenerationAdvanced advanced{
        SweepPreviewCursor{nextCursor_}, nextGeneration,
        streamStatus(nextStatus, nullptr)};
    PreviewEvent nextEvent{advanced};
    generation_ = nextGeneration;
    status_ = nextStatus;
    activeIdentity_.reset();
    currentPreview_.reset();
    ++nextCursor_;
    latestEvent_.swap(nextEvent);
    changed_.notify_all();
    return advanced;
}

bool SweepPreviewExchange::invalidate(
    SweepPreviewIdentity identity) noexcept {
    std::lock_guard lock{mutex_};
    if (!activeIdentity_.has_value() || *activeIdentity_ != identity) {
        return false;
    }
    PreviewEvent nextEvent{SweepPreviewInvalidated{
        SweepPreviewCursor{nextCursor_}, identity,
        streamStatus(status_, nullptr)}};
    activeIdentity_.reset();
    currentPreview_.reset();
    ++nextCursor_;
    latestEvent_.swap(nextEvent);
    changed_.notify_all();
    return true;
}

std::optional<SweepPreviewEvent> SweepPreviewExchange::waitForNext(
    SweepPreviewCursor after,
    vna::compat::StopToken token) const {
    std::optional<SweepPreviewEvent> result;
    {
        vna::compat::StopCallback notify{token, [this] {
            std::lock_guard lock{mutex_};
            changed_.notify_all();
        }};
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [&] {
            return token.stopRequested() ||
                (latestEvent_.has_value() &&
                 cursorOf(*latestEvent_).value > after.value);
        });
        if (!token.stopRequested()) {
            result = latestEvent_;
        }
        lock.unlock();
    }
    return result;
}

}  // namespace vna::application
