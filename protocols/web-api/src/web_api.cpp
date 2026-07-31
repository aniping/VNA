#include <vna/web_api/web_api.hpp>

#include "display_frame_json_codec.hpp"
#include "json_codec.hpp"
#include "web_asset_path.hpp"

#include <httplib.h>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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
        // Missing Trace and not-yet-published frame intentionally share one
        // empty response; the Web adapter does not reconstruct query policy.
        response.status = httplib::StatusCode::NoContent_204;
        return;
    }
    constexpr std::size_t maximumResponseBytes = 131'072;
    const auto body = detail::encodeDisplayFrame(**frame);
    if (body.size() > maximumResponseBytes) {
        // The repository's 2048-point contract should keep this unreachable.
        // Fail closed if a future codec change breaks the wire-size boundary.
        response.status = httplib::StatusCode::InternalServerError_500;
        response.set_content(
            R"({"error":"internal-error"})", "application/json");
        return;
    }
    response.set_content(body, "application/json");
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
        const application::TraceDisplayFrameQuery& displayFrames,
        const std::optional<std::filesystem::path>& webRoot)
        : commandBus_(commandBus), displayFrames_(displayFrames) {
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
    const application::TraceDisplayFrameQuery& displayFrames_;
    httplib::Server server_;
};

WebApi::WebApi(
    application::CommandBus& commandBus,
    const application::TraceDisplayFrameQuery& displayFrames,
    std::optional<std::filesystem::path> webRoot)
    : impl_([&] {
          const auto validated = detail::validateWebRoot(webRoot);
          return std::make_unique<Impl>(commandBus, displayFrames, validated);
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
    server_.Get(
        R"(/api/v1/traces/([^/]*)/display-frame)",
        [this](const httplib::Request& request, httplib::Response& response) {
            handleDisplayFrame(displayFrames_, request, response);
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
