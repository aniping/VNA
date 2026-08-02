#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <vna/application/command_bus.hpp>
#include <vna/application/operation_manager.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::observability {
class Logger;
}

namespace vna::web_api {

using LogFailureReporter = std::function<void(std::string_view)>;

struct WebApiOptions {
    std::optional<std::filesystem::path> webRoot;
    // Non-owning; when non-null it must outlive WebApi and may be called by
    // concurrent HTTP handlers. Reads and frame delivery are deliberately quiet.
    observability::Logger* logger{};
    // Required when logger is set. This independent callable must not use that
    // logger or its sinks; it must support concurrent calls, outlive WebApi's
    // captured references, and eventually return. Thrown exceptions are ignored.
    LogFailureReporter logFailureReporter;
};

class WebApi {
public:
    // A configured web root must contain regular index.html and assets entries.
    // Only the two index routes and /assets/ are served; there is no root mount.
    // Its trusted directory tree must remain immutable for this object's life.
    // All borrowed application dependencies must outlive this adapter.
    explicit WebApi(
        application::CommandBus& commandBus,
        application::OperationManager& operations,
        const application::TraceDisplayFrameQuery& displayFrames,
        const application::TraceDisplayFrameRepository& displayRepository,
        WebApiOptions options = {});
    ~WebApi();

    WebApi(const WebApi&) = delete;
    WebApi& operator=(const WebApi&) = delete;

    [[nodiscard]] bool listen(const std::string& address, int port);
    [[nodiscard]] int bindToAnyPort(const std::string& address);
    [[nodiscard]] bool listenAfterBind();
    void waitUntilReady();
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna::web_api
