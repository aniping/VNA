#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

StateSnapshot initialSnapshot(const FactoryPreset& preset) {
    return {
        .stateRevision = 0,
        .control = {},
        .instrument = preset.commandBusState.instrument.snapshot(),
        .display = preset.commandBusState.displayWorkspace.snapshot(),
    };
}

CommandEnvelope measurementCommand(
    display_model::TraceId traceId,
    domain::MeasurementType type,
    const char* commandId,
    std::uint64_t expectedRevision,
    const char* instrumentId = "instrument-1") {
    return {
        .commandId = CommandId{commandId},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{instrumentId},
        .origin = CommandOrigin::Web,
        .expectedStateRevision = expectedRevision,
        .payload = SetTraceMeasurementTypeCommand{traceId, type},
    };
}

CommandErrorCode errorCode(const CommandResult& result) {
    return commandErrorCode(std::get<CommandError>(result.outcome));
}

class TraceMeasurementTypeCommandHardeningTest : public ::testing::Test {
protected:
    TraceMeasurementTypeCommandHardeningTest()
        : catalog_(domain::ChannelId{1}, repository_, initialSnapshot(preset_)),
          bus_(InstrumentId{"instrument-1"},
               vna::test::stoppedSingleSweepHandler(),
               catalog_,
               std::move(preset_.commandBusState)) {}

    TraceDisplayFrameSetHandle publishCurrent(std::uint64_t sequence) {
        const auto plan = catalog_.capture();
        const auto& target = plan->targets.front();
        auto published = catalog_.publishIfCurrent(
            plan,
            TraceDisplayFrameSet{
                plan->generation,
                sequence,
                {{frames::FrameId{sequence},
                  target.trace.id,
                  target.measurement.id,
                  target.measurement.type,
                  plan->stateRevision,
                  plan->generation,
                  sequence,
                  target.trace.format,
                  {1.0, 2.0},
                  CartesianTraceDisplaySamples{
                      TraceDisplayUnit::Decibel, {-10.0, -11.0}}}}});
        return std::get<TraceDisplayFrameSetHandle>(std::move(published));
    }

    FactoryPreset preset_{makeFactoryPreset()};
    TraceDisplayFrameRepository repository_{4};
    TracePublicationCatalog catalog_;
    CommandBus bus_;
};

TEST_F(
    TraceMeasurementTypeCommandHardeningTest,
    ReplaysExactResultAndRejectsDifferentTypeReuse) {
    const auto command = measurementCommand(
        display_model::TraceId{1}, domain::MeasurementType::S11, "stable", 0);
    const auto first = bus_.dispatch(command);
    const auto retained = publishCurrent(1);
    const auto replay = bus_.dispatch(command);
    auto reused = command;
    reused.payload = SetTraceMeasurementTypeCommand{
        display_model::TraceId{1}, domain::MeasurementType::S12};
    const auto rejected = bus_.dispatch(reused);

    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(first.outcome));
    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(replay.outcome));
    EXPECT_EQ(
        std::get<CommandSuccess>(replay.outcome).value,
        std::get<CommandSuccess>(first.outcome).value);
    EXPECT_EQ(replay.stateRevision, first.stateRevision);
    EXPECT_EQ(errorCode(rejected), CommandErrorCode::CommandIdReuse);
    EXPECT_EQ(bus_.snapshot().stateRevision, 1U);
    EXPECT_EQ(catalog_.capture()->generation, 2U);
    EXPECT_EQ(repository_.latestFrameSet(), retained);
}

TEST_F(
    TraceMeasurementTypeCommandHardeningTest,
    EveryRejectedPathPreservesPlanFramesAndConfiguration) {
    const auto retained = publishCurrent(1);
    const auto missing = bus_.dispatch(measurementCommand(
        display_model::TraceId{99}, domain::MeasurementType::S11, "missing", 0));
    const auto invalid = bus_.dispatch(measurementCommand(
        display_model::TraceId{1},
        static_cast<domain::MeasurementType>(99), "invalid", 0));
    const auto conflict = bus_.dispatch(measurementCommand(
        display_model::TraceId{1}, domain::MeasurementType::S11, "conflict", 99));
    const auto wrong = bus_.dispatch(measurementCommand(
        display_model::TraceId{1}, domain::MeasurementType::S11,
        "wrong", 0, "other-instrument"));

    EXPECT_EQ(errorCode(missing), CommandErrorCode::TraceNotFound);
    EXPECT_EQ(errorCode(invalid), CommandErrorCode::TraceConfigurationRejected);
    EXPECT_EQ(errorCode(conflict), CommandErrorCode::StateRevisionConflict);
    EXPECT_EQ(errorCode(wrong), CommandErrorCode::WrongInstrument);
    ASSERT_TRUE(std::holds_alternative<ControlSnapshot>(
        bus_.tryAttachScpiSession(SessionId{"owner"}, [] {}).outcome));
    ASSERT_TRUE(std::holds_alternative<ControlSnapshot>(
        bus_.activateScpiControl(SessionId{"owner"}).outcome));
    const auto denied = bus_.dispatch(measurementCommand(
        display_model::TraceId{1}, domain::MeasurementType::S11, "denied", 1));
    EXPECT_EQ(errorCode(denied), CommandErrorCode::ControlDenied);

    const auto state = bus_.snapshot();
    EXPECT_EQ(state.stateRevision, 1U);
    EXPECT_EQ(state.instrument.measurements.size(), 1U);
    ASSERT_EQ(state.display.traces.size(), 1U);
    EXPECT_EQ(state.display.traces[0].measurementId, domain::MeasurementId{1});
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    EXPECT_EQ(repository_.latestFrameSet(), retained);
}

}  // namespace
}  // namespace vna::application
