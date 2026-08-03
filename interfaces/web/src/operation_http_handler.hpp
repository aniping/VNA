#pragma once

#include <vna/application/operation_manager.hpp>

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace vna::web_api::detail {

// Operation status is the progress-polling seam. Clients wait here for a
// terminal state, then retrieve display-frame once instead of polling frames
// and conflating "still running" with "no data".
void handleOperation(
    const application::OperationManager& operations,
    const httplib::Request& request,
    httplib::Response& response);

}  // namespace vna::web_api::detail
