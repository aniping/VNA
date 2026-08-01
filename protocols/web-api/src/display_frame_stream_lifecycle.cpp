#include "display_frame_stream_internal.hpp"

#include <array>

namespace vna::web_api::detail {

bool DisplayFrameStream::Impl::beginListen() noexcept {
    std::lock_guard lock{mutex_};
    if (stopping_) {
        return false;
    }
    ++activeListeners_;
    ++pendingListenerStarts_;
    return true;
}

void DisplayFrameStream::Impl::listenerStarted() noexcept {
    {
        std::lock_guard lock{mutex_};
        if (pendingListenerStarts_ > 0) {
            --pendingListenerStarts_;
        }
    }
    sessionsChanged_.notify_all();
}

void DisplayFrameStream::Impl::waitUntilListenerStarted() noexcept {
    std::unique_lock lock{mutex_};
    sessionsChanged_.wait(
        lock, [this] { return pendingListenerStarts_ == 0; });
}

void DisplayFrameStream::Impl::finishListen() noexcept {
    {
        std::lock_guard lock{mutex_};
        if (pendingListenerStarts_ > 0) {
            --pendingListenerStarts_;
        }
        --activeListeners_;
    }
    sessionsChanged_.notify_all();
}

void DisplayFrameStream::Impl::requestStop() noexcept {
    std::array<
        std::shared_ptr<DisplayFrameStreamSession>, maximumSessions> sessions;
    std::size_t count = 0;
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
        for (const auto& [id, session] : sessions_) {
            static_cast<void>(id);
            sessions[count++] = session;
        }
    }
    // Cancellation invokes repository callbacks and close_now touches the
    // transport, so both remain outside the registry lock.
    for (std::size_t index = 0; index < count; ++index) {
        sessions[index]->requestStopAndClose(
            {httplib::ws::CloseStatus::GoingAway, "server stopping"});
    }
}

void DisplayFrameStream::Impl::waitUntilStopped() noexcept {
    std::unique_lock lock{mutex_};
    sessionsChanged_.wait(lock, [this] {
        return activeListeners_ == 0 && activeHandlers_ == 0;
    });
}

DisplayFrameStream::DisplayFrameStream(
    const application::TraceDisplayFrameRepository& repository)
    : impl_(std::make_unique<Impl>(repository)) {}

DisplayFrameStream::~DisplayFrameStream() {
    requestStop();
    waitUntilStopped();
}

void DisplayFrameStream::install(httplib::Server& server) { impl_->install(server); }
bool DisplayFrameStream::beginListen() noexcept { return impl_->beginListen(); }
void DisplayFrameStream::waitUntilListenerStarted() noexcept {
    impl_->waitUntilListenerStarted();
}
void DisplayFrameStream::finishListen() noexcept { impl_->finishListen(); }
void DisplayFrameStream::requestStop() noexcept { impl_->requestStop(); }
void DisplayFrameStream::waitUntilStopped() noexcept { impl_->waitUntilStopped(); }

}  // namespace vna::web_api::detail
