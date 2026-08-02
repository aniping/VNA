#include <vna/application/factory_preset.hpp>

#include <chrono>
#include <cstdint>
#include <utility>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

// The simulation source is synchronous pure computation. A positive product
// cadence prevents a tight loop and gives the first display roughly 10 Hz;
// it is a display pacing choice, not a hardware capability claim.
constexpr auto kSimulationMinimumSweepPeriod = 100ms;
constexpr display_model::CartesianScaleSettings kFactoryLogMagnitudeScale{
    .scalePerDivision = 10.0,
    .referenceValue = 0.0,
    .referencePosition = 9.0,
};

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
    domain::ChannelId acquisitionChannelId;
    display_model::TraceId defaultTraceId;
};

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
        display_model::TraceFormat::LogMagnitude,
        // ZNB system Preset places 0 dB on the second major line from the top.
        // Supplying the three independent values here avoids changing the
        // ordinary createTrace default used outside the product preset.
        kFactoryLogMagnitudeScale).value();
    return {
        .commandBusState = {
            .instrument = std::move(instrument),
            .displayWorkspace = std::move(displayWorkspace),
        },
        .acquisitionChannelId = channel,
        .defaultTraceId = trace,
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
        .acquisitionChannelId = state.acquisitionChannelId,
        .defaultTraceId = state.defaultTraceId,
    };
}

}  // namespace vna::application
