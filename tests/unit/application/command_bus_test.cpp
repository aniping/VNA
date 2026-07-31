#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <utility>

#include <vna/application/command_bus.hpp>

namespace vna::application {
namespace {

constexpr domain::SweepSettings validSweep() {
    return {
        .startFrequencyHz = 1'000'000'000,
        .stopFrequencyHz = 2'000'000'000,
        .points = 201,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
    };
}

CommandEnvelope makeCommand(
    std::string commandId,
    std::uint64_t revision,
    CommandPayload payload,
    InstrumentId instrumentId = InstrumentId{"instrument-1"}) {
    return {
        .commandId = CommandId{std::move(commandId)},
        .sessionId = SessionId{"session-1"},
        .instrumentId = std::move(instrumentId),
        .expectedStateRevision = revision,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = std::move(payload),
    };
}

domain::TraceId createTrace(CommandBus& commandBus) {
    const auto channelResult = commandBus.dispatch(makeCommand(
        "setup-channel",
        0,
        CreateChannelCommand{validSweep()}));
    const auto channelId = std::get<domain::ChannelId>(channelResult.value);
    const auto measurementResult = commandBus.dispatch(makeCommand(
        "setup-measurement",
        1,
        CreateMeasurementCommand{channelId, domain::MeasurementType::S11}));
    const auto measurementId =
        std::get<domain::MeasurementId>(measurementResult.value);
    const auto windowResult = commandBus.dispatch(
        makeCommand("setup-window", 2, CreateWindowCommand{}));
    const auto windowId = std::get<domain::WindowId>(windowResult.value);
    const auto traceResult = commandBus.dispatch(makeCommand(
        "setup-trace",
        3,
        CreateTraceCommand{
            windowId,
            measurementId,
            domain::TraceFormat::LogMagnitude,
        }));
    return std::get<domain::TraceId>(traceResult.value);
}

TEST(CommandBusTest, SuccessfulCommandIncrementsRevisionOnce) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto command =
        makeCommand("command-1", 0, CreateChannelCommand{validSweep()});

    const auto result = commandBus.dispatch(command);

    EXPECT_EQ(result.status, CommandStatus::Succeeded);
    EXPECT_EQ(result.stateRevision, 1U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 1U);
    EXPECT_EQ(snapshot.instrument.channels.size(), 1U);
}

TEST(CommandBusTest, InvalidCommandDoesNotChangeStateOrRevision) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto command = makeCommand(
        "command-1",
        0,
        CreateChannelCommand{domain::SweepSettings{
            .startFrequencyHz = 2'000'000'000,
            .stopFrequencyHz = 1'000'000'000,
            .points = 201,
            .ifBandwidthHz = 1'000,
            .powerDbm = -10.0,
        }});

    const auto result = commandBus.dispatch(command);

    EXPECT_EQ(result.status, CommandStatus::ValidationError);
    EXPECT_EQ(result.stateRevision, 0U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 0U);
    EXPECT_TRUE(snapshot.instrument.channels.empty());
}

TEST(CommandBusTest, StaleRevisionIsRejectedWithoutChangingState) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto firstCommand =
        makeCommand("command-1", 0, CreateChannelCommand{validSweep()});
    ASSERT_EQ(
        commandBus.dispatch(firstCommand).status,
        CommandStatus::Succeeded);
    const auto staleCommand =
        makeCommand("command-2", 0, CreateChannelCommand{validSweep()});

    const auto result = commandBus.dispatch(staleCommand);

    EXPECT_EQ(result.status, CommandStatus::Conflict);
    EXPECT_EQ(result.stateRevision, 1U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 1U);
    EXPECT_EQ(snapshot.instrument.channels.size(), 1U);
}

TEST(CommandBusTest, CommandForAnotherInstrumentIsRejected) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto command = makeCommand(
        "command-1",
        0,
        CreateChannelCommand{validSweep()},
        InstrumentId{"instrument-2"});

    const auto result = commandBus.dispatch(command);

    EXPECT_EQ(result.status, CommandStatus::WrongInstrument);
    EXPECT_EQ(result.stateRevision, 0U);
    EXPECT_TRUE(commandBus.snapshot().instrument.channels.empty());
}

TEST(CommandBusTest, CreatesMeasurementAndTraceThroughUnifiedEntryPoint) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto channelResult = commandBus.dispatch(makeCommand(
        "command-1",
        0,
        CreateChannelCommand{validSweep()}));
    ASSERT_EQ(channelResult.status, CommandStatus::Succeeded);
    const auto channelId = std::get<domain::ChannelId>(channelResult.value);

    const auto measurementResult = commandBus.dispatch(makeCommand(
        "command-2",
        1,
        CreateMeasurementCommand{channelId, domain::MeasurementType::S11}));
    ASSERT_EQ(measurementResult.status, CommandStatus::Succeeded);
    const auto measurementId =
        std::get<domain::MeasurementId>(measurementResult.value);

    const auto windowResult = commandBus.dispatch(
        makeCommand("command-3", 2, CreateWindowCommand{}));
    ASSERT_EQ(windowResult.status, CommandStatus::Succeeded);
    const auto windowId = std::get<domain::WindowId>(windowResult.value);

    const auto traceResult = commandBus.dispatch(makeCommand(
        "command-4",
        3,
        CreateTraceCommand{
            windowId,
            measurementId,
            domain::TraceFormat::LogMagnitude,
        }));
    ASSERT_EQ(traceResult.status, CommandStatus::Succeeded);

    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 4U);
    EXPECT_EQ(snapshot.instrument.channels.size(), 1U);
    EXPECT_EQ(snapshot.instrument.measurements.size(), 1U);
    EXPECT_EQ(snapshot.instrument.windows.size(), 1U);
    EXPECT_EQ(snapshot.instrument.traces.size(), 1U);
}

TEST(CommandBusTest, RemovesTraceThroughUnifiedEntryPoint) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto traceId = createTrace(commandBus);

    const auto result = commandBus.dispatch(
        makeCommand("command-5", 4, RemoveTraceCommand{traceId}));

    EXPECT_EQ(result.status, CommandStatus::Succeeded);
    EXPECT_EQ(result.stateRevision, 5U);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result.value));
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.instrument.measurements.size(), 1U);
    EXPECT_TRUE(snapshot.instrument.traces.empty());
}

}  // namespace
}  // namespace vna::application
