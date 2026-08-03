#pragma once

#include <memory>
#include <optional>
#include <string>

#include <vna/application/command_bus.hpp>
#include <vna/application/operation_manager.hpp>
#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/compat/filesystem.hpp>

namespace vna::web_api {

struct WebApiOptions {
    std::optional<vna::compat::filesystem::path> webRoot;
};

// Both retained display truths are mandatory and borrowed. Grouping them
// keeps the adapter constructor within the project's parameter limit without
// introducing ownership, fallback behavior, or another stream coordinator.
struct DisplayStreamSources {
    const application::TraceDisplayFrameRepository& completeFrames;
    const application::SweepPreviewExchange& sweepPreviews;
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
        DisplayStreamSources streamSources,
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
