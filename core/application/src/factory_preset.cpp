#include <vna/application/factory_preset.hpp>

#include <cstdint>
#include <utility>

namespace vna::application {
namespace {

constexpr domain::SweepSettings factorySweepSettings() {
    return {
        .startFrequencyHz = 10'000'000,
        .stopFrequencyHz = 26'500'000'000,
        .points = 201,
        .ifBandwidthHz = 10'000,
        .powerDbm = -10.0,
    };
}

CommandBusInitialState makeInitialState(
    const domain::SweepSettings& sweep) {
    domain::Instrument instrument;
    const auto channel = instrument.createChannel(sweep).value();
    const auto measurement = instrument.createMeasurement(
        channel, domain::MeasurementType::S21).value();
    display_model::DisplayWorkspace displayWorkspace;
    const auto window = displayWorkspace.createWindow();
    static_cast<void>(displayWorkspace.createTrace(
        window,
        measurement,
        display_model::TraceFormat::LogMagnitude).value());
    return {
        .instrument = std::move(instrument),
        .displayWorkspace = std::move(displayWorkspace),
    };
}

}  // namespace

FactoryPreset makeFactoryPreset() {
    const auto sweep = factorySweepSettings();
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
        },
        .commandBusState = makeInitialState(sweep),
    };
}

}  // namespace vna::application
