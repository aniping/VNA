#include <vna/application/sweep_preview_exchange.hpp>

#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vna::application {
namespace {

using PreviewEvent = std::optional<SweepPreviewEvent>;
static_assert(std::is_nothrow_swappable_v<PreviewEvent>);

}  // namespace

bool SweepPreviewExchange::validStatus(
    const SweepRuntimeDisplayStatus& status) noexcept {
    if (status.generation == 0 || status.channelId.value() == 0 ||
        status.progress.totalPoints == 0 ||
        status.progress.completedPoints > status.progress.totalPoints) {
        return false;
    }
    if (status.userPhase == SweepUserPhase::Preparing) {
        return status.progress.completedPoints == 0;
    }
    if (status.userPhase == SweepUserPhase::Hold) {
        return !status.sweepId.has_value() &&
            status.progress.completedPoints == status.progress.totalPoints;
    }
    if (status.userPhase == SweepUserPhase::Calculation) {
        return status.sweepId.has_value() &&
            status.progress.completedPoints == status.progress.totalPoints;
    }
    if (status.userPhase == SweepUserPhase::Sweeping) {
        return status.sweepId.has_value() &&
            status.progress.completedPoints != 0;
    }
    return true;
}

SweepPreviewStreamStatus SweepPreviewExchange::streamStatus(
    const SweepRuntimeDisplayStatus& runtime,
    const SweepPreviewHandle& preview) {
    return {runtime, preview == nullptr
                         ? std::optional<SweepPreviewIdentity>{}
                         : std::optional{preview->identity}};
}

SweepPreviewExchange::SweepPreviewExchange(
    SweepRuntimeDisplayStatus initialStatus)
    : status_(std::move(initialStatus)) {
    if (!validStatus(status_)) {
        throw std::invalid_argument{"invalid initial sweep status"};
    }
    generation_ = status_.generation;
    latestEvent_ = SweepPreviewStatusChanged{
        SweepPreviewCursor{nextCursor_++}, streamStatus(status_, nullptr)};
}

void SweepPreviewExchange::updateForRuntime(
    SweepRuntimeDisplayStatus status) noexcept {
    if (!validStatus(status)) {
        std::terminate();
    }
    std::lock_guard lock{mutex_};
    if (status.generation != generation_ || status == status_) {
        return;
    }
    auto stream = streamStatus(status, currentPreview_);
    PreviewEvent nextEvent = currentPreview_ == nullptr
        ? PreviewEvent{SweepPreviewStatusChanged{
              SweepPreviewCursor{nextCursor_}, std::move(stream)}}
        : PreviewEvent{SweepPreviewAvailable{
              SweepPreviewCursor{nextCursor_}, currentPreview_,
              std::move(stream)}};
    status_ = std::move(status);
    ++nextCursor_;
    latestEvent_.swap(nextEvent);
    changed_.notify_all();
}

bool SweepPreviewExchange::invalidateForRuntime(
    SweepPreviewIdentity identity,
    SweepRuntimeDisplayStatus status) noexcept {
    if (!validStatus(status) || status.generation != identity.generation) {
        std::terminate();
    }
    std::lock_guard lock{mutex_};
    if (generation_ != identity.generation) {
        return false;
    }
    const auto invalidated =
        activeIdentity_.has_value() && *activeIdentity_ == identity;
    PreviewEvent nextEvent;
    if (invalidated) {
        nextEvent = SweepPreviewInvalidated{
            SweepPreviewCursor{nextCursor_}, identity,
            streamStatus(status, nullptr)};
        activeIdentity_.reset();
        currentPreview_.reset();
    } else if (status != status_) {
        auto stream = streamStatus(status, currentPreview_);
        nextEvent = currentPreview_ == nullptr
            ? PreviewEvent{SweepPreviewStatusChanged{
                  SweepPreviewCursor{nextCursor_}, std::move(stream)}}
            : PreviewEvent{SweepPreviewAvailable{
                  SweepPreviewCursor{nextCursor_}, currentPreview_,
                  std::move(stream)}};
    } else {
        return false;
    }
    status_ = std::move(status);
    ++nextCursor_;
    latestEvent_.swap(nextEvent);
    changed_.notify_all();
    return invalidated;
}

bool SweepPreviewExchange::matchesInitialStatus(
    const SweepRuntimeDisplayStatus& expected) const noexcept {
    std::lock_guard lock{mutex_};
    return status_.generation == expected.generation &&
        status_.channelId == expected.channelId &&
        status_.stateRevision == expected.stateRevision &&
        status_.progress.totalPoints == expected.progress.totalPoints &&
        nextCursor_ == 2;
}

}  // namespace vna::application
