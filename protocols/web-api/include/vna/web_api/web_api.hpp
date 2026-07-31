#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <vna/application/command_bus.hpp>

namespace vna::web_api {

class WebApi {
public:
    // A configured web root must contain regular index.html and assets entries.
    // Only the two index routes and /assets/ are served; there is no root mount.
    // Its trusted directory tree must remain immutable for this object's life.
    explicit WebApi(
        application::CommandBus& commandBus,
        std::optional<std::filesystem::path> webRoot = std::nullopt);
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
