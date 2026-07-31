#include <vna/web_api/web_api.hpp>

#include <httplib.h>

namespace vna::web_api {

WebApi::WebApi(application::CommandBus& commandBus)
    : commandBus_(commandBus) {}

void WebApi::install(httplib::Server& server) {
    server.Get(
        "/api/v1/health",
        [](const httplib::Request&, httplib::Response& response) {
            response.set_content(R"({"status":"ok"})", "application/json");
        });
}

}  // namespace vna::web_api
