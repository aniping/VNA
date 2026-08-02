#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string_view>
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
#include <vna/logging/json_lines_logger.hpp>
#include <vna/observability/logger.hpp>
#include <vna/platform/executable_path.hpp>
#include <vna/server/startup_observability.hpp>
#include <vna/simulation/simulation_sweep.hpp>
#include <vna/web_api/web_api.hpp>

namespace {

constexpr auto instrumentId = "instrument-1";
constexpr std::size_t traceCapacity = 1024;
// A stable product seed makes the simulated frame sequence reproducible across
// restarts without leaking simulation concerns into the acquisition plan.
constexpr std::uint64_t simulationSeed = 0x564E4101ULL;

void reportEmergency(std::string_view message) noexcept {
    // stderr is deliberately outside the logger and its std::cout/file sinks.
    // Do not flush here: the first release cannot promise cancellable I/O.
    static_cast<void>(std::fwrite(message.data(), 1, message.size(), stderr));
    static_cast<void>(std::fputc('\n', stderr));
}

bool flushLogs(
    vna::observability::Logger& logger,
    std::string_view failureMessage) {
    if (logger.flush()) {
        return true;
    }
    reportEmergency(failureMessage);
    return false;
}

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

int serveUntilStopped(
    vna::web_api::WebApi& webApi,
    vna::observability::Logger& logger) {
    if (!vna::server::writeStartupMilestones(logger, instrumentId)) {
        reportEmergency("vna-server failed to record startup milestones");
        static_cast<void>(flushLogs(logger, "vna-server startup log flush failed"));
        return EXIT_FAILURE;
    }
    // Publish the starting event before listen blocks, so operators can
    // distinguish a live server from an empty log without relying on exit.
    if (!flushLogs(logger, "vna-server startup log flush failed")) {
        return EXIT_FAILURE;
    }
    constexpr auto address = "127.0.0.1";
    constexpr int port = 8080;
    if (!webApi.listen(address, port)) {
        static_cast<void>(vna::server::writeListenFailed(logger, instrumentId));
        static_cast<void>(flushLogs(logger, "vna-server final log flush failed"));
        return EXIT_FAILURE;
    }
    const auto stoppedRecorded =
        vna::server::writeStopped(logger, instrumentId);
    const auto flushed = flushLogs(logger, "vna-server final log flush failed");
    return stoppedRecorded && flushed ? EXIT_SUCCESS : EXIT_FAILURE;
}

std::unique_ptr<vna::observability::Logger> makeServerLogger(
    const std::filesystem::path& logDirectory) {
    auto options = vna::logging::JsonLinesLoggerOptions{logDirectory};
    // The same structured event feeds a human console formatter and the
    // authoritative JSONL file; launch scripts own only three location hints.
    options.consoleFormat = vna::logging::ConsoleFormat::HumanReadable;
    return vna::logging::makeJsonLinesLogger(options);
}

struct ServerPaths {
    std::filesystem::path webRoot;
    std::filesystem::path logDirectory;
};

ServerPaths serverPaths() {
    const auto executable = vna::platform::currentExecutablePath();
    const auto releaseRoot = executable.parent_path().parent_path();
    return {releaseRoot / "web", releaseRoot / "logs"};
}

int runServer() {
    const auto paths = serverPaths();

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
    // Construct it before the logger so a broken release never creates logs.
    vna::web_api::WebApi webApi{
        commandBus,
        operationManager,
        displayFrameQuery,
        publication.repository,
        paths.webRoot};
    auto logger = makeServerLogger(paths.logDirectory);
    try {
        return serveUntilStopped(webApi, *logger);
    } catch (...) {
        reportEmergency("vna-server failed during logged execution");
        return EXIT_FAILURE;
    }
}

}  // namespace

int main() {
    try {
        return runServer();
    } catch (const std::exception& error) {
        reportEmergency(error.what());
        return EXIT_FAILURE;
    } catch (...) {
        reportEmergency("vna-server startup failed");
        return EXIT_FAILURE;
    }
}
