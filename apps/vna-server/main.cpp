#include <cstddef>
#include <iostream>
#include <stop_token>
#include <utility>

#include <vna/application/command_bus.hpp>
#include <vna/application/operation_manager.hpp>
#include <vna/application/single_sweep_command_handler.hpp>
#include <vna/application/single_sweep_executor.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/simulation/simulation_sweep.hpp>
#include <vna/web_api/web_api.hpp>

int main() {
    constexpr std::size_t traceCapacity = 1024;
    constexpr std::size_t sweepQueueCapacity = 16;
    vna::application::OperationManager operationManager;
    vna::application::TraceDisplayFrameRepository frameRepository{
        traceCapacity};
    vna::application::RawSweepSource sweepSource =
        [](const vna::frames::FrequencyAxis& axis, std::stop_token) {
            return vna::simulation::simulateSweep(axis);
        };
    vna::application::SingleSweepExecutor sweepExecutor{
        sweepQueueCapacity,
        std::move(sweepSource),
        operationManager,
        frameRepository};
    vna::application::SingleSweepCommandHandler sweepHandler{
        [&sweepExecutor](vna::application::SingleSweepWorkItem work) {
            return sweepExecutor.submit(std::move(work));
        }};
    vna::application::CommandBus commandBus{
        vna::application::InstrumentId{"instrument-1"}, sweepHandler};
    vna::application::TraceDisplayFrameQuery displayFrameQuery{
        commandBus, frameRepository};
    vna::web_api::WebApi webApi{
        commandBus, operationManager, displayFrameQuery};

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
