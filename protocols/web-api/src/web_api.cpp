#include <vna/web_api/web_api.hpp>

#include "json_codec.hpp"
#include "web_asset_path.hpp"

#include <httplib.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

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

void serveIndex(
    const std::filesystem::path& webRoot,
    httplib::Response& response) {
    const auto index = detail::resolveWebAsset(webRoot, "index.html");
    if (!index) {
        response.status = httplib::StatusCode::NotFound_404;
        return;
    }
    response.set_header("Cache-Control", "no-cache");
    response.set_file_content(*index, "text/html; charset=utf-8");
}

void serveAsset(
    const std::filesystem::path& assetsRoot,
    const httplib::Request& request,
    httplib::Response& response) {
    const auto asset = detail::resolveWebAsset(
        assetsRoot, request.matches[1].str());
    if (!asset) {
        response.status = httplib::StatusCode::NotFound_404;
        return;
    }
    response.set_header(
        "Cache-Control", "public, max-age=31536000, immutable");
    response.set_file_content(*asset);
}

}  // namespace

class WebApi::Impl {
public:
    Impl(
        application::CommandBus& commandBus,
        const std::optional<std::filesystem::path>& webRoot)
        : commandBus_(commandBus) {
        installRoutes();
        if (webRoot) {
            installIndexRoutes(*webRoot);
            installAssets(*webRoot / "assets");
        }
    }

    void installRoutes();
    void installIndexRoutes(std::filesystem::path indexPath);
    void installAssets(const std::filesystem::path& assetsPath);

    application::CommandBus& commandBus_;
    httplib::Server server_;
};

WebApi::WebApi(
    application::CommandBus& commandBus,
    std::optional<std::filesystem::path> webRoot)
    : impl_([&] {
          const auto validated = detail::validateWebRoot(webRoot);
          return std::make_unique<Impl>(commandBus, validated);
      }()) {}

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

void WebApi::Impl::installIndexRoutes(std::filesystem::path indexPath) {
    const auto handler = [indexPath = std::move(indexPath)](
                             const httplib::Request&,
                             httplib::Response& response) {
        serveIndex(indexPath, response);
    };
    // Keep entry points explicit: mounting the entire root would make release
    // files participate in every route lookup and weaken the /api separation.
    server_.Get("/", handler);
    server_.Get("/index.html", handler);
}

void WebApi::Impl::installAssets(const std::filesystem::path& assetsPath) {
    auto canonicalRoot = std::filesystem::canonical(assetsPath);
    // Only this namespace reaches the filesystem. Avoiding a root mount keeps
    // /api outside static lookup and prevents an accidental SPA fallback.
    server_.Get(
        R"(/assets/(.+))",
        [canonicalRoot = std::move(canonicalRoot)](
            const httplib::Request& request,
            httplib::Response& response) {
            serveAsset(canonicalRoot, request, response);
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
