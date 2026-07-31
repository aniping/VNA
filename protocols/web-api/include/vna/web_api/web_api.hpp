#pragma once

#include <memory>
#include <string>

#include <vna/application/command_bus.hpp>

namespace vna::web_api {

class WebApi {
public:
    explicit WebApi(application::CommandBus& commandBus);
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
