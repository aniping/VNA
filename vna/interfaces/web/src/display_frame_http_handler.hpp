#pragma once

namespace httplib {
struct Request;
struct Response;
}

namespace vna::application {
class TraceDisplayFrameQuery;
}

namespace vna::web_api::detail {

void handleDisplayFrame(
    const application::TraceDisplayFrameQuery& displayFrames,
    const httplib::Request& request,
    httplib::Response& response);

}  // namespace vna::web_api::detail
