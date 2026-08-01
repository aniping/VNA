#include <vna/application/trace_display_frame_repository.hpp>

#include "trace_display_frame_validation_internal.hpp"

#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vna::application {
namespace {

TraceDisplayFrameSetError setError(
    TraceDisplayFrameSetErrorCode code,
    std::optional<TraceDisplayFrameError> frameError = std::nullopt) {
    return {.code = code, .frameError = frameError};
}

bool metadataMatches(
    const TraceDisplayFrame& frame,
    const TraceDisplayFrameSet& set,
    const TraceDisplayFrame& first) {
    return frame.generation == set.generation &&
           frame.sequenceNumber == set.sequenceNumber &&
           frame.frameId == first.frameId &&
           frame.stateRevision == first.stateRevision &&
           frame.frequenciesHz == first.frequenciesHz;
}

std::optional<TraceDisplayFrameSetError> validateSet(
    const TraceDisplayFrameSet& set,
    std::size_t capacity) {
    if (set.frames.empty()) {
        return setError(TraceDisplayFrameSetErrorCode::EmptyFrameSet);
    }
    if (set.generation == 0) {
        return setError(TraceDisplayFrameSetErrorCode::InvalidGeneration);
    }
    if (set.sequenceNumber == 0) {
        return setError(TraceDisplayFrameSetErrorCode::InvalidSequenceNumber);
    }
    if (set.frames.size() > capacity) {
        return setError(TraceDisplayFrameSetErrorCode::CapacityExceeded);
    }
    std::unordered_set<std::uint64_t> traceIds;
    traceIds.reserve(set.frames.size());
    const auto& first = set.frames.front();
    for (const auto& frame : set.frames) {
        if (const auto error = internal::validateTraceDisplayFrame(frame)) {
            return setError(TraceDisplayFrameSetErrorCode::InvalidFrame, error);
        }
        if (!metadataMatches(frame, set, first)) {
            return setError(
                TraceDisplayFrameSetErrorCode::FrameMetadataMismatch);
        }
        if (!traceIds.insert(frame.traceId.value()).second) {
            return setError(TraceDisplayFrameSetErrorCode::DuplicateTraceId);
        }
    }
    return std::nullopt;
}

using TraceMap =
    std::unordered_map<std::uint64_t, TraceDisplayFrameHandle>;

TraceMap makeTraceMap(const TraceDisplayFrameSetHandle& set) {
    TraceMap frames;
    frames.reserve(set->frames.size());
    for (const auto& frame : set->frames) {
        // The alias keeps the whole immutable set alive while exposing the
        // existing per-Trace handle without copying a potentially large frame.
        frames.emplace(
            frame.traceId.value(),
            TraceDisplayFrameHandle{set, &frame});
    }
    return frames;
}

}  // namespace

TraceDisplayFrameSetResult TraceDisplayFrameRepository::publishFrameSet(
    TraceDisplayFrameSet frameSet) {
    if (const auto error = validateSet(frameSet, capacity_)) {
        return *error;
    }
    auto published =
        std::make_shared<const TraceDisplayFrameSet>(std::move(frameSet));
    auto newFrames = makeTraceMap(published);
    std::vector<std::shared_ptr<WaitState>> notify;
    // Sequence admission and both retained views change under one lock, so a
    // reader can see either complete publication but never a mixed set.
    {
        std::lock_guard lock{mutex_};
        if (published->generation < generation_) {
            return setError(TraceDisplayFrameSetErrorCode::StaleGeneration);
        }
        if (published->generation > generation_) {
            return setError(TraceDisplayFrameSetErrorCode::FutureGeneration);
        }
        if (latestFrameSet_ != nullptr &&
            published->sequenceNumber < latestFrameSet_->sequenceNumber) {
            return setError(TraceDisplayFrameSetErrorCode::SequenceRegression);
        }
        if (latestFrameSet_ != nullptr &&
            published->sequenceNumber == latestFrameSet_->sequenceNumber) {
            return *published == *latestFrameSet_
                ? TraceDisplayFrameSetResult{latestFrameSet_}
                : TraceDisplayFrameSetResult{
                      setError(TraceDisplayFrameSetErrorCode::SequenceConflict)};
        }
        for (const auto& [traceId, state] : waitStates_) {
            if (latestByTrace_.contains(traceId) &&
                !newFrames.contains(traceId)) {
                ++state->discardGeneration;
            }
            notify.push_back(state);
        }
        latestByTrace_.swap(newFrames);
        latestFrameSet_ = published;
    }
    // Waiter callbacks are notified after publication and outside the mutex;
    // awakened readers may immediately re-enter the repository.
    for (const auto& state : notify) {
        state->changed.notify_all();
    }
    return published;
}

TraceDisplayGenerationResult TraceDisplayFrameRepository::advanceGeneration(
    std::uint64_t nextGeneration) {
    std::vector<std::shared_ptr<WaitState>> notify;
    {
        std::lock_guard lock{mutex_};
        if (generation_ == std::numeric_limits<std::uint64_t>::max() ||
            nextGeneration != generation_ + 1) {
            return setError(TraceDisplayFrameSetErrorCode::GenerationNotNext);
        }
        generation_ = nextGeneration;
        latestFrameSet_.reset();
        latestByTrace_.clear();
        // A generation boundary invalidates every legacy per-Trace view too;
        // bumping its discard epoch lets existing waits observe that boundary.
        for (const auto& [traceId, state] : waitStates_) {
            static_cast<void>(traceId);
            ++state->discardGeneration;
            notify.push_back(state);
        }
    }
    // Notification stays lock-free for the same re-entrancy reason as publish.
    for (const auto& state : notify) {
        state->changed.notify_all();
    }
    return GenerationAdvanced{nextGeneration};
}

TraceDisplayFrameSetHandle TraceDisplayFrameRepository::latestFrameSet() const {
    std::lock_guard lock{mutex_};
    return latestFrameSet_;
}

}  // namespace vna::application
