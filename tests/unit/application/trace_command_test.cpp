#include <gtest/gtest.h>

#include <chrono>
#include <utility>

#include <vna/application/command_bus.hpp>

namespace vna::application {
namespace {

bool isSuccess(const CommandResult& result) {
    return std::holds_alternative<CommandSuccess>(result.outcome);
}

CommandEnvelope command(
    const char* id,
    std::uint64_t revision,
    CommandPayload payload) {
    return {
        .commandId = CommandId{id},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = revision,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
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
                domain::WindowId{1},
                domain::MeasurementId{1},
                domain::TraceFormat::LogMagnitude}))));
}

TEST(TraceCommandTest, UpdatesTraceFormatAndIncrementsRevisionOnce) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    createTrace(commandBus);

    const auto result = commandBus.dispatch(command(
        "update-trace-format",
        4,
        UpdateTraceFormatCommand{
            domain::TraceId{1},
            domain::TraceFormat::Phase}));

    EXPECT_TRUE(isSuccess(result));
    EXPECT_EQ(result.stateRevision, 5U);
    EXPECT_EQ(
        std::get<domain::TraceId>(
            std::get<CommandSuccess>(result.outcome).value),
        domain::TraceId{1});
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 5U);
    ASSERT_EQ(snapshot.instrument.traces.size(), 1U);
    EXPECT_EQ(snapshot.instrument.traces[0].format, domain::TraceFormat::Phase);
}

}  // namespace
}  // namespace vna::application
