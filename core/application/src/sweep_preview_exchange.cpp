#include <vna/application/sweep_preview_exchange.hpp>

#include "sweep_preview_validation_internal.hpp"

#include <utility>

namespace vna::application {
namespace {

SweepPreviewCursor cursorOf(const SweepPreviewEvent& event) {
    return std::visit(
        [](const auto& value) { return value.cursor; }, event);
}

}  // namespace

SweepPreviewPublishResult SweepPreviewExchange::publish(
    SweepPreview preview) {
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
        const auto continuing =
            activeIdentity_.has_value() &&
            *activeIdentity_ == preview.identity;
        if (continuing && currentPreview_ != nullptr &&
            !internal::isCumulativeExtension(*currentPreview_, preview)) {
            return SweepPreviewError{SweepPreviewErrorCode::ProgressRegression};
        }
        if (!continuing && sweepId <= lastSweepId_) {
            return SweepPreviewError{SweepPreviewErrorCode::SweepIdRegression};
        }
        if (!continuing) {
            activeIdentity_ = preview.identity;
            lastSweepId_ = sweepId;
        }
        handle = std::make_shared<const SweepPreview>(std::move(preview));
        currentPreview_ = handle;
        latestEvent_ = SweepPreviewAvailable{
            SweepPreviewCursor{nextCursor_++}, handle};
    }
    changed_.notify_all();
    return handle;
}

SweepPreviewGenerationResult SweepPreviewExchange::advanceGeneration(
    std::uint64_t nextGeneration) {
    SweepPreviewGenerationAdvanced advanced;
    {
        std::lock_guard lock{mutex_};
        if (nextGeneration == 0 || nextGeneration != generation_ + 1) {
            return SweepPreviewError{SweepPreviewErrorCode::GenerationNotNext};
        }
        generation_ = nextGeneration;
        activeIdentity_.reset();
        currentPreview_.reset();
        advanced = {SweepPreviewCursor{nextCursor_++}, generation_};
        latestEvent_ = advanced;
    }
    changed_.notify_all();
    return advanced;
}

bool SweepPreviewExchange::invalidate(
    SweepPreviewIdentity identity) noexcept {
    {
        std::lock_guard lock{mutex_};
        if (!activeIdentity_.has_value() || *activeIdentity_ != identity) {
            return false;
        }
        activeIdentity_.reset();
        currentPreview_.reset();
        latestEvent_ = SweepPreviewInvalidated{
            SweepPreviewCursor{nextCursor_++}, identity};
    }
    changed_.notify_all();
    return true;
}

std::optional<SweepPreviewEvent> SweepPreviewExchange::waitForNext(
    SweepPreviewCursor after,
    std::stop_token token) const {
    std::optional<SweepPreviewEvent> result;
    {
        // Cancellation notification takes the predicate mutex so it cannot
        // race between the predicate check and the condition-variable wait.
        std::stop_callback notify{token, [this] {
            std::lock_guard lock{mutex_};
            changed_.notify_all();
        }};
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [&] {
            return token.stop_requested() ||
                   (latestEvent_.has_value() &&
                    cursorOf(*latestEvent_).value > after.value);
        });
        if (!token.stop_requested()) {
            result = latestEvent_;
        }
        // A running stop callback can wait for this lock, so release it before
        // destroying the callback registration.
        lock.unlock();
    }
    return result;
}

}  // namespace vna::application
