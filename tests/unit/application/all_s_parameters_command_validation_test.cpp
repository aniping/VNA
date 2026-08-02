#include <gtest/gtest.h>

#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

CommandEnvelope ensureCommand() {
    return {
        CommandId{"all-s"}, SessionId{"session-1"},
        InstrumentId{"instrument-1"}, CommandOrigin::Web, 0,
        EnsureAllSParametersCommand{display_model::TraceId{1}},
    };
}

TEST(AllSParametersCommandValidationTest, ReusesExistingMeasurements) {
    auto preset = makeFactoryPreset();
    auto& instrument = preset.commandBusState.instrument;
    const auto s11 = instrument.createMeasurement(
        domain::ChannelId{1}, domain::MeasurementType::S11).value();
    const auto s12 = instrument.createMeasurement(
        domain::ChannelId{1}, domain::MeasurementType::S12).value();
    const auto s22 = instrument.createMeasurement(
        domain::ChannelId{1}, domain::MeasurementType::S22).value();
    vna::test::CommandBusRuntimeOwner runtimeOwner{
        preset.commandBusState, 8};
    CommandBus bus{InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler(), runtimeOwner.catalog(),
        std::move(preset.commandBusState)};

    const auto result = bus.dispatch(ensureCommand());
    const auto state = bus.snapshot();
    EXPECT_NE(std::get_if<CommandSuccess>(&result.outcome), nullptr);
    ASSERT_EQ(state.instrument.measurements.size(), 4U);
    EXPECT_EQ(state.instrument.measurements[1].id, s11);
    EXPECT_EQ(state.instrument.measurements[2].id, s12);
    EXPECT_EQ(state.instrument.measurements[3].id, s22);
    EXPECT_EQ(state.display.windows.size(), 4U);
    EXPECT_EQ(state.display.traces.size(), 4U);
}

TEST(AllSParametersCommandValidationTest, RejectsPartialDisplayAtomically) {
    auto preset = makeFactoryPreset();
    const auto measurement = preset.commandBusState.instrument.createMeasurement(
        domain::ChannelId{1}, domain::MeasurementType::S11).value();
    const auto window = preset.commandBusState.displayWorkspace.createWindow();
    ASSERT_TRUE(preset.commandBusState.displayWorkspace.createTrace(
        window, measurement, display_model::TraceFormat::LogMagnitude).hasValue());
    vna::test::CommandBusRuntimeOwner runtimeOwner{
        preset.commandBusState, 8};
    const auto initialPlan = runtimeOwner.catalog().capture();
    CommandBus bus{InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler(), runtimeOwner.catalog(),
        std::move(preset.commandBusState)};

    const auto rejected = bus.dispatch(ensureCommand());

    const auto* error = std::get_if<CommandError>(&rejected.outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(std::get<ApplicationError>(*error).code,
        ApplicationErrorCode::TraceConfigurationRejected);
    EXPECT_EQ(rejected.stateRevision, 0U);
    EXPECT_EQ(runtimeOwner.catalog().capture(), initialPlan);
    EXPECT_EQ(bus.snapshot().display.traces.size(), 2U);
}

}  // namespace
}  // namespace vna::application
