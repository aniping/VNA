#include "display_frame_http_handler.hpp"

#include "display_frame_json_codec.hpp"

#include <httplib.h>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include <vna/application/trace_display_frame_query.hpp>

namespace vna::web_api::detail {
namespace {

std::optional<display_model::TraceId> parseTraceId(std::string_view text) {
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value == 0) {
        return std::nullopt;
    }
    return display_model::TraceId{value};
}

}  // namespace

void handleDisplayFrame(
    const application::TraceDisplayFrameQuery& displayFrames,
    const httplib::Request& request,
    httplib::Response& response) {
    const auto traceId = parseTraceId(request.matches[1].str());
    if (!traceId) {
        response.status = httplib::StatusCode::BadRequest_400;
        response.set_content(
            R"({"error":"invalid-trace-id"})", "application/json");
        return;
    }

    response.set_header("Cache-Control", "no-store");
    const auto outcome = displayFrames.latest(*traceId);
    const auto* frame = std::get_if<application::TraceDisplayFrameHandle>(
        &outcome);
    if (frame == nullptr) {
        const auto error = std::get<
            application::TraceDisplayFrameQueryError>(outcome);
        if (error.code == application::
                              TraceDisplayFrameQueryErrorCode::TraceNotFound) {
            response.status = httplib::StatusCode::NotFound_404;
            response.set_content(
                R"({"error":"trace-not-found"})", "application/json");
        } else {
            response.status = httplib::StatusCode::NoContent_204;
        }
        return;
    }
    const auto body = encodeDisplayFrame(**frame);
    if (body.size() > maximumDisplayFrameResponseBytes) {
        // A valid 2048-point frame fits today. Keep a fail-closed boundary if
        // a later encoder change expands the public response unexpectedly.
        response.status = httplib::StatusCode::InternalServerError_500;
        response.set_content(
            R"({"error":"internal-error"})", "application/json");
        return;
    }
    response.set_content(body, "application/json");
}

}  // namespace vna::web_api::detail
