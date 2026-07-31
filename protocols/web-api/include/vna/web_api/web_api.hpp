#pragma once

#include <vna/application/command_bus.hpp>

namespace httplib {
class Server;
}

namespace vna::web_api {

class WebApi {
public:
    explicit WebApi(application::CommandBus& commandBus);

    void install(httplib::Server& server);

private:
    application::CommandBus& commandBus_;
};

}  // namespace vna::web_api
