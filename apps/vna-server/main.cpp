#include <httplib.h>

#include <iostream>

#include <vna/application/command_bus.hpp>
#include <vna/web_api/web_api.hpp>

int main() {
    vna::application::CommandBus commandBus{
        vna::application::InstrumentId{"instrument-1"}};
    vna::web_api::WebApi webApi{commandBus};
    httplib::Server server;
    webApi.install(server);

    constexpr auto address = "127.0.0.1";
    constexpr int port = 8080;
    std::cout << "vna-server listening on http://" << address << ':' << port
              << '\n';
    if (!server.listen(address, port)) {
        std::cerr << "vna-server failed to listen\n";
        return 1;
    }
    return 0;
}
