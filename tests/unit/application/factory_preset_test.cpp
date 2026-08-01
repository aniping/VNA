#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

CommandEnvelope command(
    std::string commandId,
    std::uint64_t revision,
    CommandPayload payload) {
    return {
        .commandId = CommandId{std::move(commandId)},
        .sessionId = SessionId{"factory-preset-test"},
        .instrumentId = InstrumentId{"instrument-1"},
        .origin = CommandOrigin::Web,
        .expectedStateRevision = revision,
        .payload = std::move(payload),
    };
}

template <typename Value>
Value successValue(const CommandResult& result) {
    return std::get<Value>(std::get<CommandSuccess>(result.outcome).value);
}

TEST(FactoryPresetTest, CreatesCorrelatedS21StateAtRevisionZero) {
    auto preset = makeFactoryPreset();
    const auto& plan = preset.acquisitionPlan;
    EXPECT_EQ(plan.frequencyAxis.startFrequencyHz, 10'000'000U);
    EXPECT_EQ(plan.frequencyAxis.stopFrequencyHz, 26'500'000'000U);
    EXPECT_EQ(plan.frequencyAxis.points, 201U);
    EXPECT_EQ(plan.frequencyAxis.id, frames::FrequencyAxisId{1});
    EXPECT_EQ(plan.portCount, 2U);
    EXPECT_EQ(plan.ifBandwidthHz, 10'000U);
    EXPECT_DOUBLE_EQ(plan.powerDbm, -10.0);
    ASSERT_EQ(plan.sourcePorts.size(), 2U);
    EXPECT_EQ(plan.sourcePorts[0], 1U);
    EXPECT_EQ(plan.sourcePorts[1], 2U);

    CommandBus bus{
        InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler(),
        std::move(preset.commandBusState)};
    const auto snapshot = bus.snapshot();

    EXPECT_EQ(snapshot.stateRevision, 0U);
    ASSERT_EQ(snapshot.instrument.channels.size(), 1U);
    const auto& channel = snapshot.instrument.channels[0];
    EXPECT_EQ(channel.id, domain::ChannelId{1});
    EXPECT_EQ(channel.sweep.startFrequencyHz, plan.frequencyAxis.startFrequencyHz);
    EXPECT_EQ(channel.sweep.stopFrequencyHz, plan.frequencyAxis.stopFrequencyHz);
    EXPECT_EQ(channel.sweep.points, plan.frequencyAxis.points);
    EXPECT_EQ(channel.sweep.ifBandwidthHz, plan.ifBandwidthHz);
    EXPECT_DOUBLE_EQ(channel.sweep.powerDbm, plan.powerDbm);
    EXPECT_EQ(channel.sweepMode, domain::SweepMode::Continuous);
    EXPECT_EQ(channel.triggerSource, domain::TriggerSource::None);

    ASSERT_EQ(snapshot.instrument.measurements.size(), 1U);
    const auto& measurement = snapshot.instrument.measurements[0];
    EXPECT_EQ(measurement.id, domain::MeasurementId{1});
    EXPECT_EQ(measurement.channelId, domain::ChannelId{1});
    EXPECT_EQ(measurement.type, domain::MeasurementType::S21);
    ASSERT_EQ(snapshot.display.windows.size(), 1U);
    EXPECT_EQ(snapshot.display.windows[0].id, display_model::WindowId{1});
    ASSERT_EQ(snapshot.display.traces.size(), 1U);
    const auto& trace = snapshot.display.traces[0];
    EXPECT_EQ(trace.id, display_model::TraceId{1});
    EXPECT_EQ(trace.windowId, display_model::WindowId{1});
    EXPECT_EQ(trace.measurementId, domain::MeasurementId{1});
    EXPECT_EQ(trace.format, display_model::TraceFormat::LogMagnitude);
    ASSERT_TRUE(trace.scale.has_value());
    EXPECT_DOUBLE_EQ(trace.scale->scalePerDivision, 10.0);
    EXPECT_DOUBLE_EQ(trace.scale->referenceValue, 0.0);
    EXPECT_DOUBLE_EQ(trace.scale->referencePosition, 8.0);
    EXPECT_DOUBLE_EQ(trace.scale->minimum, -80.0);
    EXPECT_DOUBLE_EQ(trace.scale->maximum, 20.0);
}

TEST(FactoryPresetTest, FirstMutationsUseRevisionOneAndNextIdentifiers) {
    auto preset = makeFactoryPreset();
    CommandBus bus{
        InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler(),
        std::move(preset.commandBusState)};
    const domain::SweepSettings sweep{
        .startFrequencyHz = 20'000'000,
        .stopFrequencyHz = 1'000'000'000,
        .points = 101,
        .ifBandwidthHz = 1'000,
        .powerDbm = -20.0,
    };

    const auto channel = bus.dispatch(
        command("create-channel", 0, CreateChannelCommand{sweep}));
    EXPECT_EQ(channel.stateRevision, 1U);
    EXPECT_EQ(successValue<domain::ChannelId>(channel), domain::ChannelId{2});
    const auto channels = bus.snapshot().instrument.channels;
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_EQ(channels[1].sweepMode, domain::SweepMode::Continuous);
    EXPECT_EQ(channels[1].triggerSource, domain::TriggerSource::None);
    const auto measurement = bus.dispatch(command(
        "create-measurement",
        1,
        CreateMeasurementCommand{
            domain::ChannelId{1}, domain::MeasurementType::S11}));
    EXPECT_EQ(measurement.stateRevision, 2U);
    EXPECT_EQ(
        successValue<domain::MeasurementId>(measurement),
        domain::MeasurementId{2});
    const auto window = bus.dispatch(
        command("create-window", 2, CreateWindowCommand{}));
    EXPECT_EQ(window.stateRevision, 3U);
    EXPECT_EQ(
        successValue<display_model::WindowId>(window),
        display_model::WindowId{2});
    const auto trace = bus.dispatch(command(
        "create-trace",
        3,
        CreateTraceCommand{
            display_model::WindowId{1},
            domain::MeasurementId{1},
            display_model::TraceFormat::LogMagnitude}));
    EXPECT_EQ(trace.stateRevision, 4U);
    EXPECT_EQ(
        successValue<display_model::TraceId>(trace),
        display_model::TraceId{2});
}

TEST(FactoryPresetTest, ExplicitEmptyStateRemainsAvailable) {
    CommandBus bus{
        InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler(),
        CommandBusInitialState{}};

    const auto snapshot = bus.snapshot();

    EXPECT_EQ(snapshot.stateRevision, 0U);
    EXPECT_TRUE(snapshot.instrument.channels.empty());
    EXPECT_TRUE(snapshot.instrument.measurements.empty());
    EXPECT_TRUE(snapshot.display.windows.empty());
    EXPECT_TRUE(snapshot.display.traces.empty());
}

}  // namespace
}  // namespace vna::application
