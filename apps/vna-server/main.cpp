#include <iostream>

#include <vna/application/command_bus.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/web_api/web_api.hpp>

int main() {
    vna::application::CommandBus commandBus{
        vna::application::InstrumentId{"instrument-1"}};
    vna::application::TraceDisplayFrameRepository displayFrames{4};
    vna::application::TraceDisplayFrameQuery displayFrameQuery{
        commandBus, displayFrames};
    vna::web_api::WebApi webApi{commandBus, displayFrameQuery};

    constexpr auto address = "127.0.0.1";
    constexpr int port = 8080;
    std::cout << "vna-server listening on http://" << address << ':' << port
              << '\n';
    if (!webApi.listen(address, port)) {
        std::cerr << "vna-server failed to listen\n";
        return 1;
    }
    return 0;
}
