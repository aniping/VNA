#include "display_frame_stream_internal.hpp"

#include "display_frame_json_codec.hpp"

#include <string>
#include <thread>
#include <variant>

namespace vna::web_api::detail {
namespace {

void closeSocket(
    httplib::ws::WebSocket& socket, DisplayStreamClose action) noexcept {
    try {
        socket.close_now(action.status, action.reason);
    } catch (...) {
        // A broken peer cannot prevent registry cleanup or server shutdown.
    }
}

}  // namespace

void DisplayFrameStream::Impl::install(httplib::Server& server) {
    // The start callback closes the brief interval after beginListen where
    // Server::stop would otherwise see httplib as not running and do nothing.
    server.set_start_handler([this] { listenerStarted(); });
    server.WebSocket(
        "/api/v1/display-frames",
        [this](const httplib::Request&, httplib::ws::WebSocket& socket) {
            serve(socket);
        });
}

std::shared_ptr<DisplayFrameStreamSession>
DisplayFrameStream::Impl::registerSession(
    httplib::ws::WebSocket& socket, DisplayStreamClose& rejection) noexcept {
    std::lock_guard lock{mutex_};
    ++activeHandlers_;
    if (stopping_) {
        rejection = {
            httplib::ws::CloseStatus::GoingAway, "server stopping"};
        return {};
    }
    if (sessions_.size() >= maximumSessions) {
        rejection = {
            httplib::ws::CloseStatus::PolicyViolation,
            "display stream capacity reached"};
        return {};
    }
    try {
        const auto id = nextSessionId_;
        auto session =
            std::make_shared<DisplayFrameStreamSession>(id, socket);
        sessions_.emplace(id, session);
        ++nextSessionId_;
        return session;
    } catch (...) {
        rejection = {httplib::ws::CloseStatus::InternalError,
                     "display stream unavailable"};
        return {};
    }
}

void DisplayFrameStream::Impl::finishRejectedHandler() noexcept {
    {
        std::lock_guard lock{mutex_};
        --activeHandlers_;
    }
    sessionsChanged_.notify_all();
}

void DisplayFrameStream::Impl::finishSession(
    const std::shared_ptr<DisplayFrameStreamSession>& session) noexcept {
    // Close the borrowed socket lifetime before the registry releases its
    // shared control state. Concurrent stop then observes a null socket.
    {
        std::lock_guard socketLock{session->socketMutex};
        session->socket = nullptr;
    }
    {
        std::lock_guard lock{mutex_};
        sessions_.erase(session->id);
        --activeHandlers_;
    }
    sessionsChanged_.notify_all();
}

void DisplayFrameStream::Impl::streamFrames(
    httplib::ws::WebSocket& socket, std::stop_token token) noexcept {
    DisplayStreamClose action{
        httplib::ws::CloseStatus::GoingAway, "display stream ended"};
    try {
        std::uint64_t lastSent = 0;
        while (!token.stop_requested()) {
            const auto outcome = query_.waitForNext(traceId_, lastSent, token);
            if (token.stop_requested()) {
                action.reason = "server stopping";
                break;
            }
            const auto* frame =
                std::get_if<application::TraceDisplayFrameHandle>(&outcome);
            if (frame == nullptr || (*frame)->sequenceNumber <= lastSent) {
                action = {httplib::ws::CloseStatus::PolicyViolation,
                          "display trace unavailable"};
                break;
            }
            const auto message = encodeDisplayFrame(**frame);
            if (message.size() > maximumDisplayFrameResponseBytes) {
                action = {httplib::ws::CloseStatus::MessageTooBig,
                          "display frame too large"};
                break;
            }
            if (!socket.send(message)) {
                break;
            }
            lastSent = (*frame)->sequenceNumber;
        }
    } catch (...) {
        action = {httplib::ws::CloseStatus::InternalError,
                  "display stream failed"};
    }
    closeSocket(socket, action);
}

void DisplayFrameStream::Impl::serve(httplib::ws::WebSocket& socket) noexcept {
    DisplayStreamClose rejection{
        httplib::ws::CloseStatus::PolicyViolation,
        "display stream unavailable"};
    const auto session = registerSession(socket, rejection);
    if (!session) {
        closeSocket(socket, rejection);
        finishRejectedHandler();
        return;
    }
    try {
        // The handler is the only reader; the worker only waits and writes. A
        // client Close wakes this thread, which joins the worker before
        // httplib destroys the socket and any TLS stream.
        std::jthread worker{[this, &socket](std::stop_token token) {
            streamFrames(socket, token);
        }};
        std::stop_callback cancelWorker{
            session->stop.get_token(), [&worker] { worker.request_stop(); }};
        std::string ignored;
        while (socket.read(ignored) != httplib::ws::ReadResult::Fail) {
            ignored.clear();
        }
        static_cast<void>(session->stop.request_stop());
        worker.join();
    } catch (...) {
        closeSocket(
            socket, {httplib::ws::CloseStatus::InternalError,
                     "display session failed"});
    }
    finishSession(session);
}

}  // namespace vna::web_api::detail
