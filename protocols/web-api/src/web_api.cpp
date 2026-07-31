#include <vna/web_api/web_api.hpp>

#include "json_codec.hpp"

#include <httplib.h>

#include <memory>
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

class WebApi::Impl {
public:
    explicit Impl(application::CommandBus& commandBus)
        : commandBus_(commandBus) {
        installRoutes();
    }

    void installRoutes();

    application::CommandBus& commandBus_;
    httplib::Server server_;
};

WebApi::WebApi(application::CommandBus& commandBus)
    : impl_(std::make_unique<Impl>(commandBus)) {}

WebApi::~WebApi() = default;

void WebApi::Impl::installRoutes() {
    server_.Get(
        "/api/v1/health",
        [](const httplib::Request&, httplib::Response& response) {
            response.set_content(R"({"status":"ok"})", "application/json");
        });
    server_.Get(
        "/api/v1/state",
        [this](const httplib::Request&, httplib::Response& response) {
            response.set_content(
                detail::encodeState(commandBus_.snapshot()),
                "application/json");
        });
    server_.Post(
        "/api/v1/commands",
        [this](const httplib::Request& request, httplib::Response& response) {
            handleCommand(commandBus_, request, response);
        });
}

bool WebApi::listen(const std::string& address, int port) {
    return impl_->server_.listen(address, port);
}

int WebApi::bindToAnyPort(const std::string& address) {
    return impl_->server_.bind_to_any_port(address);
}

bool WebApi::listenAfterBind() {
    return impl_->server_.listen_after_bind();
}

void WebApi::waitUntilReady() {
    impl_->server_.wait_until_ready();
}

void WebApi::stop() {
    impl_->server_.stop();
}

}  // namespace vna::web_api
