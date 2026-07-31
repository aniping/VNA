#include <vna/web_api/web_api.hpp>

#include "json_codec.hpp"

#include <httplib.h>

#include <string>

namespace vna::web_api {
namespace {

void rejectInvalidCommand(httplib::Response& response) {
    response.status = httplib::StatusCode::BadRequest_400;
    response.set_content(R"({"error":"invalidCommand"})", "application/json");
}

void handleCommand(
    application::CommandBus& commandBus,
    const httplib::Request& request,
    httplib::Response& response) {
    const auto command = detail::decodeCommand(request.body);
    if (!command) {
        rejectInvalidCommand(response);
        return;
    }
    const auto result = detail::encodeCommandResult(
        commandBus.dispatch(*command));
    response.status = result.httpStatus;
    response.set_content(result.body, "application/json");
}

}  // namespace

WebApi::WebApi(application::CommandBus& commandBus)
    : commandBus_(commandBus) {}

void WebApi::install(httplib::Server& server) {
    server.Get(
        "/api/v1/health",
        [](const httplib::Request&, httplib::Response& response) {
            response.set_content(R"({"status":"ok"})", "application/json");
        });
    server.Get(
        "/api/v1/state",
        [this](const httplib::Request&, httplib::Response& response) {
            response.set_content(
                detail::encodeState(commandBus_.snapshot()),
                "application/json");
        });
    server.Post(
        "/api/v1/commands",
        [this](const httplib::Request& request, httplib::Response& response) {
            handleCommand(commandBus_, request, response);
        });
}

}  // namespace vna::web_api
