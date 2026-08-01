#pragma once

#include <memory>

#include <vna/application/trace_display_frame_query.hpp>

namespace httplib {
class Server;
}

namespace vna::web_api::detail {

// Owns only transport sessions. The query and its application dependencies
// must outlive this adapter; stopping a session never stops frame production.
class DisplayFrameStream {
public:
    DisplayFrameStream(
        const application::TraceDisplayFrameQuery& query,
        display_model::TraceId traceId);
    ~DisplayFrameStream();

    DisplayFrameStream(const DisplayFrameStream&) = delete;
    DisplayFrameStream& operator=(const DisplayFrameStream&) = delete;

    void install(httplib::Server& server);
    [[nodiscard]] bool beginListen() noexcept;
    void waitUntilListenerStarted() noexcept;
    void finishListen() noexcept;
    // Stop is split around listener shutdown: requestStop rejects any handler
    // that races with Server::stop, then waitUntilStopped joins every handler
    // that entered before the listener was closed.
    void requestStop() noexcept;
    void waitUntilStopped() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna::web_api::detail
