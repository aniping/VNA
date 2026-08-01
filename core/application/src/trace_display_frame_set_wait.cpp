#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::application {

std::optional<TraceDisplayFrameSetEvent>
TraceDisplayFrameRepository::waitForNextSet(
    TraceDisplayFrameSetCursor cursor,
    std::stop_token token) const {
    std::optional<TraceDisplayFrameSetEvent> result;
    {
        // Cancellation is external state. Synchronizing notification through
        // the predicate mutex closes the check-to-sleep lost-wakeup window.
        std::stop_callback notify{token, [this] {
            std::lock_guard lock{mutex_};
            frameSetChanged_.notify_all();
        }};
        std::unique_lock lock{mutex_};
        frameSetChanged_.wait(lock, [&] {
            return token.stop_requested() ||
                   generation_ != cursor.generation ||
                   (latestFrameSet_ != nullptr &&
                    latestFrameSet_->sequenceNumber > cursor.sequenceNumber);
        });
        if (!token.stop_requested()) {
            if (generation_ != cursor.generation) {
                result = GenerationAdvanced{generation_};
            } else {
                result = FrameSetAvailable{latestFrameSet_};
            }
        }
        // stop_callback destruction may wait for an active callback; release
        // the mutex first so a cancelling thread can always finish notifying.
        lock.unlock();
    }
    return result;
}

}  // namespace vna::application
