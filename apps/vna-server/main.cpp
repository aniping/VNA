#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <stop_token>
#include <utility>

#include <vna/acquisition/continuous_acquisition.hpp>
#include <vna/application/command_bus.hpp>
#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/application/disabled_single_sweep_execution.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/operation_manager.hpp>
#include <vna/application/single_sweep_command_handler.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/platform/executable_path.hpp>
#include <vna/simulation/simulation_sweep.hpp>
#include <vna/web_api/web_api.hpp>

namespace {

constexpr auto instrumentId = "instrument-1";
constexpr auto webAddress = "127.0.0.1";
constexpr int webPort = 8080;
constexpr std::size_t traceCapacity = 1024;
// A stable product seed makes the simulated frame sequence reproducible across
// restarts without leaking simulation concerns into the acquisition plan.
constexpr std::uint64_t simulationSeed = 0x564E4101ULL;

vna::acquisition::RawSweepSource makeSimulationSource() {
    return [](const vna::acquisition::ContinuousAcquisitionPlan& plan,
              std::uint64_t sequence,
              std::stop_token) {
        // ContinuousAcquisition is the sole source owner and supplies every
        // authoritative hardware input plus the monotonic engine sequence.
        return vna::simulation::simulateOpenPorts({
            .frequencyAxis = plan.frequencyAxis,
            .portCount = plan.portCount,
            .ifBandwidthHz = plan.ifBandwidthHz,
            .powerDbm = plan.powerDbm,
            .seed = simulationSeed,
            .sequenceNumber = sequence,
        });
    };
}

vna::application::StateSnapshot presetSnapshot(
    const vna::application::FactoryPreset& preset) {
    return {
        .stateRevision = 0,
        .control = {},
        .instrument = preset.commandBusState.instrument.snapshot(),
        .display = preset.commandBusState.displayWorkspace.snapshot(),
    };
}

struct PublicationState {
    explicit PublicationState(const vna::application::FactoryPreset& preset)
        : repository{traceCapacity},
          catalog{preset.acquisitionChannelId,
                  repository,
                  presetSnapshot(preset)} {}

    vna::application::TraceDisplayFrameRepository repository;
    vna::application::TracePublicationCatalog catalog;
};

std::filesystem::path releaseWebRoot() {
    const auto executable = vna::platform::currentExecutablePath();
    const auto releaseRoot = executable.parent_path().parent_path();
    return releaseRoot / "web";
}

int runServer() {
    const auto webRoot = releaseWebRoot();

    auto preset = vna::application::makeFactoryPreset();
    // Declaration order is the borrowing graph. Reverse destruction stops Web
    // access first, then CommandBus and publisher, before acquisition and repos.
    vna::application::OperationManager operationManager;
    PublicationState publication{preset};
    vna::acquisition::ContinuousAcquisition acquisition{
        std::move(preset.acquisitionPlan), makeSimulationSource()};
    vna::application::ContinuousTracePublisher tracePublisher{
        acquisition, publication.catalog};
    vna::application::DisabledSingleSweepExecution disabledSingleSweep;
    vna::application::SingleSweepCommandHandler sweepHandler{
        disabledSingleSweep};
    vna::application::CommandBus commandBus{
        vna::application::InstrumentId{instrumentId},
        sweepHandler,
        publication.catalog,
        std::move(preset.commandBusState)};
    vna::application::TraceDisplayFrameQuery displayFrameQuery{
        commandBus, publication.repository};
    // WebApi validates index.html and assets without following unsafe paths.
    vna::web_api::WebApi webApi{
        commandBus,
        operationManager,
        displayFrameQuery,
        publication.repository,
        {.webRoot = webRoot}};
    return webApi.listen(webAddress, webPort) ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main() {
    try {
        return runServer();
    } catch (...) {
        return EXIT_FAILURE;
    }
}
