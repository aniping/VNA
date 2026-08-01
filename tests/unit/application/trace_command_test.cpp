#include <gtest/gtest.h>

#include <utility>

#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/display_model/display_workspace.hpp>

namespace vna::application {
namespace {

bool isSuccess(const CommandResult& result) {
    return std::holds_alternative<CommandSuccess>(result.outcome);
}

const domain::DomainError* domainError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<domain::DomainError>(error);
}

CommandEnvelope command(
    const char* id,
    std::uint64_t revision,
    CommandPayload payload) {
    return {
        .commandId = CommandId{id},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .origin = CommandOrigin::Web,
        .expectedStateRevision = revision,
        .payload = std::move(payload),
    };
}

void createTrace(CommandBus& commandBus) {
    const domain::SweepSettings sweep{
        .startFrequencyHz = 10'000'000,
        .stopFrequencyHz = 26'500'000'000,
        .points = 201,
        .ifBandwidthHz = 10'000,
        .powerDbm = -10.0,
    };
    ASSERT_TRUE(isSuccess(commandBus.dispatch(
        command("create-channel", 0, CreateChannelCommand{sweep}))));
    ASSERT_TRUE(isSuccess(commandBus.dispatch(command(
            "create-measurement",
            1,
            CreateMeasurementCommand{
                domain::ChannelId{1},
                domain::MeasurementType::S11}))));
    ASSERT_TRUE(isSuccess(commandBus.dispatch(
        command("create-window", 2, CreateWindowCommand{}))));
    ASSERT_TRUE(isSuccess(commandBus.dispatch(command(
            "create-trace",
            3,
            CreateTraceCommand{
                display_model::WindowId{1},
                domain::MeasurementId{1},
                display_model::TraceFormat::LogMagnitude}))));
}

TEST(TraceCommandTest, UpdatesTraceFormatAndIncrementsRevisionOnce) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    createTrace(commandBus);

    const auto result = commandBus.dispatch(command(
        "update-trace-format",
        4,
        UpdateTraceFormatCommand{
            display_model::TraceId{1},
            display_model::TraceFormat::Phase}));

    EXPECT_TRUE(isSuccess(result));
    EXPECT_EQ(result.stateRevision, 5U);
    EXPECT_EQ(
        std::get<display_model::TraceId>(
            std::get<CommandSuccess>(result.outcome).value),
        display_model::TraceId{1});
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 5U);
    ASSERT_EQ(snapshot.display.traces.size(), 1U);
    EXPECT_EQ(
        snapshot.display.traces[0].format,
        display_model::TraceFormat::Phase);
}

TEST(TraceCommandTest, RejectsTraceForMissingMeasurement) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    ASSERT_TRUE(isSuccess(commandBus.dispatch(
        command("create-window", 0, CreateWindowCommand{}))));

    const auto result = commandBus.dispatch(command(
        "missing-measurement",
        1,
        CreateTraceCommand{
            display_model::WindowId{1},
            domain::MeasurementId{99},
            display_model::TraceFormat::LogMagnitude}));

    ASSERT_NE(domainError(result), nullptr);
    EXPECT_EQ(
        domainError(result)->code,
        domain::DomainErrorCode::MeasurementNotFound);
    EXPECT_EQ(result.stateRevision, 1U);
    EXPECT_TRUE(commandBus.snapshot().display.traces.empty());
}

}  // namespace
}  // namespace vna::application
