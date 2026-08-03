#include "display_frame_stream_internal.hpp"

#include "display_frame_json_codec.hpp"

#include <string>
#include <variant>

namespace vna::web_api::detail {
namespace {

application::SweepPreviewCursor eventCursor(
    const application::SweepPreviewEvent& event) {
    return std::visit(
        [](const auto& value) { return value.cursor; }, event);
}

}  // namespace

void DisplayFrameStream::Impl::streamPreviews(
    httplib::ws::WebSocket& socket,
    std::stop_token token) noexcept {
    DisplayStreamClose action{
        httplib::ws::CloseStatus::GoingAway, "preview stream ended"};
    try {
        // Cursor zero deliberately asks the Exchange for its retained latest
        // event, so reconnect does not need a second snapshot interface.
        application::SweepPreviewCursor cursor{0};
        while (!token.stop_requested()) {
            const auto event = previews_.waitForNext(cursor, token);
            if (token.stop_requested()) {
                action.reason = "server stopping";
                break;
            }
            if (!event) {
                break;
            }
            const auto message = encodeSweepPreviewEvent(*event);
            if (message.size() > maximumDisplayStreamMessageBytes) {
                action = {httplib::ws::CloseStatus::MessageTooBig,
                          "sweep preview too large"};
                break;
            }
            if (!socket.send(message)) {
                break;
            }
            // Only a successful transport write advances this connection.
            // A slow peer then skips directly to the Exchange's newest event.
            cursor = eventCursor(*event);
        }
    } catch (...) {
        action = {httplib::ws::CloseStatus::InternalError,
                  "preview stream failed"};
    }
    closeSocket(socket, action);
}

}  // namespace vna::web_api::detail
