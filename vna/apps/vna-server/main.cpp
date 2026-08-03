#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <utility>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/operation_manager.hpp>
#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/platform/executable_path.hpp>
#include <vna/simulation/simulation_sweep.hpp>
#include <vna/web_api/web_api.hpp>

#include <spdlog/spdlog.h>

#include "runtime_log.hpp"

namespace {

constexpr auto instrumentId = "instrument-1";
constexpr auto webAddress = "127.0.0.1";
constexpr int webPort = 8080;
constexpr std::size_t traceCapacity = 1024;
constexpr std::uint32_t maximumPointsPerChunk = 32;
// A stable product seed makes the simulated frame sequence reproducible across
// restarts without leaking simulation concerns into the acquisition plan.
constexpr std::uint64_t simulationSeed = 0x564E4101ULL;

// Registry and formatting failures are diagnostic failures only; they cannot
// change startup, acquisition or HTTP results.
void logInfo(std::string_view message) noexcept {
    try {
        if (const auto logger = spdlog::get("vna")) {
            logger->info("{}", message);
        }
    } catch (...) {}
}

std::string_view measurementName(vna::domain::MeasurementType type) noexcept {
    switch (type) {
    case vna::domain::MeasurementType::S11: return "S11";
    case vna::domain::MeasurementType::S21: return "S21";
    case vna::domain::MeasurementType::S12: return "S12";
    case vna::domain::MeasurementType::S22: return "S22";
    }
    return "unknown";
}

void logFactoryPreset(const vna::application::FactoryPreset& preset) noexcept {
    try {
        if (const auto logger = spdlog::get("vna")) {
            const auto& plan = preset.acquisitionPlan;
            const auto display = preset.commandBusState.displayWorkspace.snapshot();
            const auto trace = std::find_if(
                display.traces.cbegin(), display.traces.cend(), [&preset](const auto& item) {
                    return item.id == preset.defaultTraceId;
                });
            if (trace == display.traces.cend()) {
                return;
            }
            const auto instrument = preset.commandBusState.instrument.snapshot();
            const auto measurement = std::find_if(
                instrument.measurements.cbegin(), instrument.measurements.cend(),
                [measurementId = trace->measurementId](const auto& item) {
                    return item.id == measurementId;
                });
            if (measurement == instrument.measurements.cend()) {
                return;
            }
            logger->info(
                "[工厂预置] 已加载：通道 {}，{}，Trace {}，频率 {} MHz 至 {} GHz，"
                "{} 点，IFBW {} kHz，功率 {} dBm",
                measurement->channelId.value(), measurementName(measurement->type),
                preset.defaultTraceId.value(),
                static_cast<double>(plan.frequencyAxis.startFrequencyHz) / 1.0e6,
                static_cast<double>(plan.frequencyAxis.stopFrequencyHz) / 1.0e9,
                plan.frequencyAxis.points, plan.ifBandwidthHz / 1'000,
                plan.powerDbm);
        }
    } catch (...) {}
}

void logWebEndpoint(spdlog::level::level_enum level) noexcept {
    try {
        if (const auto logger = spdlog::get("vna")) {
            logger->log(
                level, "[服务启动] Web 服务{}：http://{}:{}/",
                level == spdlog::level::err ? "监听失败" : "准备监听",
                webAddress, webPort);
        }
    } catch (...) {}
}

void logStartupFailure(std::string_view reason) noexcept {
    try {
        if (const auto logger = spdlog::get("vna")) {
            logger->error("[服务启动] 服务器启动失败：{}", reason);
        }
    } catch (...) {}
}

vna::acquisition::RawSweepCaptureSource makeSimulationSource() {
    return vna::simulation::makeOpenPortSweepSource({.seed = simulationSeed});
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

int runServer() {
    const auto executable = vna::platform::currentExecutablePath();
    const auto releaseRoot = executable.parent_path().parent_path();
    const auto webRoot = releaseRoot / "web";
    vna::server::initializeRuntimeLog(releaseRoot);
    logInfo("[服务启动] 矢量网络分析仪服务正在启动");

    auto preset = vna::application::makeFactoryPreset();
    logFactoryPreset(preset);
    // Declaration order is the borrowing graph. Reverse destruction stops Web
    // access and CommandBus before the sole source-owning runtime and repos.
    vna::application::OperationManager operationManager;
    PublicationState publication{preset};
    auto runtimePlan = vna::application::SweepRuntimePlan{
        std::move(preset.acquisitionPlan), publication.catalog.capture(),
        maximumPointsPerChunk};
    vna::application::SweepPreviewExchange previews{
        vna::application::initialSweepRuntimeStatus(runtimePlan)};
    vna::application::SweepRuntime sweepRuntime{
        std::move(runtimePlan),
        makeSimulationSource(), previews, publication.catalog,
        operationManager};
    logInfo("[连续扫频] 仿真持续测量已启动");
    vna::application::CommandBus commandBus{
        vna::application::InstrumentId{instrumentId},
        sweepRuntime,
        std::move(preset.commandBusState)};
    vna::application::TraceDisplayFrameQuery displayFrameQuery{
        commandBus, publication.repository};
    // WebApi validates index.html and assets without following unsafe paths.
    vna::web_api::WebApi webApi{
        commandBus,
        operationManager,
        displayFrameQuery,
        {publication.repository, previews},
        {.webRoot = webRoot}};
    logWebEndpoint(spdlog::level::info);
    if (!webApi.listen(webAddress, webPort)) {
        logWebEndpoint(spdlog::level::err);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    try {
        const auto result = runServer();
        vna::server::flushRuntimeLog();
        return result;
    } catch (const std::exception& error) {
        logStartupFailure(error.what());
        vna::server::flushRuntimeLog();
        return EXIT_FAILURE;
    } catch (...) {
        logStartupFailure("未知异常");
        vna::server::flushRuntimeLog();
        return EXIT_FAILURE;
    }
}
