#pragma once

#include "display_frame_stream.hpp"

#include <httplib.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vna/compat/stop_token.hpp>
#include <unordered_map>
namespace vna::web_api::detail {
struct DisplayStreamClose {
    httplib::ws::CloseStatus status;
    const char* reason;
};

enum class DisplayStreamKind {
    Complete,
    Preview,
};

// The registry shares this state with stop only while httplib owns socket.
// The socket gate makes handler release and external close linearizable.
struct DisplayFrameStreamSession {
    DisplayFrameStreamSession(
        std::uint64_t value, httplib::ws::WebSocket& valueSocket)
        : id(value), socket(&valueSocket) {}

    void requestStopAndClose(DisplayStreamClose action) noexcept {
        static_cast<void>(stop.requestStop());
        std::lock_guard lock{socketMutex};
        if (socket == nullptr) {
            return;
        }
        try {
            socket->close_now(action.status, action.reason);
        } catch (...) {
            // Registry cleanup still proceeds when a peer is already broken.
        }
    }

    const std::uint64_t id;
    vna::compat::StopSource stop;
    std::mutex socketMutex;
    httplib::ws::WebSocket* socket;
};

class DisplayFrameStream::Impl {
public:
    static constexpr std::size_t maximumSessions = 32;
    explicit Impl(
        const application::TraceDisplayFrameRepository& repository,
        const application::SweepPreviewExchange& previews)
        : repository_(repository), previews_(previews) {}

    void install(httplib::Server& server);
    [[nodiscard]] bool beginListen() noexcept;
    void listenerStarted() noexcept;
    void waitUntilListenerStarted() noexcept;
    void finishListen() noexcept;
    void requestStop() noexcept;
    void waitUntilStopped() noexcept;
private:
    [[nodiscard]] std::shared_ptr<DisplayFrameStreamSession> registerSession(
        httplib::ws::WebSocket& socket,
        DisplayStreamClose& rejection) noexcept;
    void finishRejectedHandler() noexcept;
    void finishSession(
        const std::shared_ptr<DisplayFrameStreamSession>& session) noexcept;
    void serve(
        httplib::ws::WebSocket& socket,
        DisplayStreamKind kind) noexcept;
    void stream(
        httplib::ws::WebSocket& socket,
        vna::compat::StopToken token,
        DisplayStreamKind kind) noexcept;
    void streamFrames(
        httplib::ws::WebSocket& socket,
        vna::compat::StopToken token) noexcept;
    void streamPreviews(
        httplib::ws::WebSocket& socket,
        vna::compat::StopToken token) noexcept;
    static void closeSocket(
        httplib::ws::WebSocket& socket,
        DisplayStreamClose action) noexcept;
    const application::TraceDisplayFrameRepository& repository_;
    const application::SweepPreviewExchange& previews_;
    std::mutex mutex_;
    std::condition_variable sessionsChanged_;
    std::unordered_map<
        std::uint64_t, std::shared_ptr<DisplayFrameStreamSession>> sessions_;
    std::uint64_t nextSessionId_{1};
    std::size_t activeHandlers_{0};
    std::size_t activeListeners_{0};
    std::size_t pendingListenerStarts_{0};
    bool stopping_{false};
};

}  // namespace vna::web_api::detail
