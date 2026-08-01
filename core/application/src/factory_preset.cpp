#include <vna/application/factory_preset.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

// The simulation source is synchronous pure computation. A positive product
// cadence prevents a tight loop and gives the first display roughly 10 Hz;
// it is a display pacing choice, not a hardware capability claim.
constexpr auto kSimulationMinimumSweepPeriod = 100ms;

constexpr domain::SweepSettings factorySweepSettings() {
    return {
        .startFrequencyHz = 10'000'000,
        .stopFrequencyHz = 26'500'000'000,
        .points = 201,
        .ifBandwidthHz = 10'000,
        .powerDbm = -10.0,
    };
}

struct FactoryState {
    CommandBusInitialState commandBusState;
    ContinuousTracePreset continuousTracePreset;
};

display_model::TraceSnapshot captureTrace(
    const display_model::DisplayWorkspace& workspace,
    display_model::TraceId traceId) {
    // Use the identity returned by creation so composition never guesses from
    // collection order when selecting its sole continuous display target.
    const auto snapshot = workspace.snapshot();
    const auto found = std::find_if(
        snapshot.traces.cbegin(), snapshot.traces.cend(),
        [traceId](const auto& trace) { return trace.id == traceId; });
    if (found == snapshot.traces.cend()) {
        throw std::logic_error{"factory trace was not created"};
    }
    return *found;
}

FactoryState makeInitialState(const domain::SweepSettings& sweep) {
    domain::Instrument instrument;
    const auto channel = instrument.createChannel(sweep).value();
    const auto measurement = instrument.createMeasurement(
        channel, domain::MeasurementType::S21).value();
    display_model::DisplayWorkspace displayWorkspace;
    const auto window = displayWorkspace.createWindow();
    const auto trace = displayWorkspace.createTrace(
        window,
        measurement,
        display_model::TraceFormat::LogMagnitude).value();
    const auto traceSnapshot = captureTrace(displayWorkspace, trace);
    return {
        .commandBusState = {
            .instrument = std::move(instrument),
            .displayWorkspace = std::move(displayWorkspace),
        },
        .continuousTracePreset = {
            .stateRevision = 0,
            .measurement = {measurement, channel, domain::MeasurementType::S21},
            .trace = traceSnapshot,
        },
    };
}

}  // namespace

FactoryPreset makeFactoryPreset() {
    const auto sweep = factorySweepSettings();
    auto state = makeInitialState(sweep);
    return {
        .acquisitionPlan = {
            .frequencyAxis = {
                .id = frames::FrequencyAxisId{1},
                .startFrequencyHz = sweep.startFrequencyHz,
                .stopFrequencyHz = sweep.stopFrequencyHz,
                .points = sweep.points,
            },
            .portCount = 2,
            .sourcePorts = {1, 2},
            .ifBandwidthHz = static_cast<std::uint32_t>(sweep.ifBandwidthHz),
            .powerDbm = sweep.powerDbm,
            .minimumSweepPeriod = kSimulationMinimumSweepPeriod,
        },
        .commandBusState = std::move(state.commandBusState),
        .continuousTracePreset = std::move(state.continuousTracePreset),
    };
}

}  // namespace vna::application
