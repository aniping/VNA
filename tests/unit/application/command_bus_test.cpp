#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <vna/application/command_bus.hpp>

namespace vna::application {
namespace {

bool isSuccess(const CommandResult& result) {
    return std::holds_alternative<CommandSuccess>(result.outcome);
}

const ApplicationError* applicationError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<ApplicationError>(error);
}

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
        .origin = CommandOrigin::Web,
        .expectedStateRevision = revision,
        .payload = std::move(payload),
    };
}

display_model::TraceId createTrace(CommandBus& commandBus) {
    const auto channelResult = commandBus.dispatch(makeCommand(
        "setup-channel",
        0,
        CreateChannelCommand{validSweep()}));
    const auto channelId = std::get<domain::ChannelId>(
        std::get<CommandSuccess>(channelResult.outcome).value);
    const auto measurementResult = commandBus.dispatch(makeCommand(
        "setup-measurement",
        1,
        CreateMeasurementCommand{channelId, domain::MeasurementType::S11}));
    const auto measurementId =
        std::get<domain::MeasurementId>(
            std::get<CommandSuccess>(measurementResult.outcome).value);
    const auto windowResult = commandBus.dispatch(
        makeCommand("setup-window", 2, CreateWindowCommand{}));
    const auto windowId = std::get<display_model::WindowId>(
        std::get<CommandSuccess>(windowResult.outcome).value);
    const auto traceResult = commandBus.dispatch(makeCommand(
        "setup-trace",
        3,
        CreateTraceCommand{
            windowId,
            measurementId,
            display_model::TraceFormat::LogMagnitude,
        }));
    return std::get<display_model::TraceId>(
        std::get<CommandSuccess>(traceResult.outcome).value);
}

TEST(CommandBusTest, SuccessfulCommandIncrementsRevisionOnce) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto command =
        makeCommand("command-1", 0, CreateChannelCommand{validSweep()});

    const auto result = commandBus.dispatch(command);

    EXPECT_TRUE(isSuccess(result));
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

    EXPECT_TRUE(std::holds_alternative<CommandError>(result.outcome));
    EXPECT_EQ(result.stateRevision, 0U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 0U);
    EXPECT_TRUE(snapshot.instrument.channels.empty());
}

TEST(CommandBusTest, StaleRevisionIsRejectedWithoutChangingState) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto firstCommand =
        makeCommand("command-1", 0, CreateChannelCommand{validSweep()});
    ASSERT_TRUE(isSuccess(commandBus.dispatch(firstCommand)));
    const auto staleCommand =
        makeCommand("command-2", 0, CreateChannelCommand{validSweep()});

    const auto result = commandBus.dispatch(staleCommand);

    ASSERT_NE(applicationError(result), nullptr);
    EXPECT_EQ(
        applicationError(result)->code,
        ApplicationErrorCode::StateRevisionConflict);
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

    ASSERT_NE(applicationError(result), nullptr);
    EXPECT_EQ(
        applicationError(result)->code,
        ApplicationErrorCode::WrongInstrument);
    EXPECT_EQ(result.stateRevision, 0U);
    EXPECT_TRUE(commandBus.snapshot().instrument.channels.empty());
}

TEST(CommandBusTest, CreatesMeasurementAndTraceThroughUnifiedEntryPoint) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto channelResult = commandBus.dispatch(makeCommand(
        "command-1",
        0,
        CreateChannelCommand{validSweep()}));
    ASSERT_TRUE(isSuccess(channelResult));
    const auto channelId = std::get<domain::ChannelId>(
        std::get<CommandSuccess>(channelResult.outcome).value);

    const auto measurementResult = commandBus.dispatch(makeCommand(
        "command-2",
        1,
        CreateMeasurementCommand{channelId, domain::MeasurementType::S11}));
    ASSERT_TRUE(isSuccess(measurementResult));
    const auto measurementId =
        std::get<domain::MeasurementId>(
            std::get<CommandSuccess>(measurementResult.outcome).value);

    const auto windowResult = commandBus.dispatch(
        makeCommand("command-3", 2, CreateWindowCommand{}));
    ASSERT_TRUE(isSuccess(windowResult));
    const auto windowId = std::get<display_model::WindowId>(
        std::get<CommandSuccess>(windowResult.outcome).value);

    const auto traceResult = commandBus.dispatch(makeCommand(
        "command-4",
        3,
        CreateTraceCommand{
            windowId,
            measurementId,
            display_model::TraceFormat::LogMagnitude,
        }));
    ASSERT_TRUE(isSuccess(traceResult));

    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 4U);
    EXPECT_EQ(snapshot.instrument.channels.size(), 1U);
    EXPECT_EQ(snapshot.instrument.measurements.size(), 1U);
    EXPECT_EQ(snapshot.display.windows.size(), 1U);
    EXPECT_EQ(snapshot.display.traces.size(), 1U);
}

TEST(CommandBusTest, RemovesTraceThroughUnifiedEntryPoint) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto traceId = createTrace(commandBus);

    const auto result = commandBus.dispatch(
        makeCommand("command-5", 4, RemoveTraceCommand{traceId}));

    EXPECT_TRUE(isSuccess(result));
    EXPECT_EQ(result.stateRevision, 5U);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(
        std::get<CommandSuccess>(result.outcome).value));
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.instrument.measurements.size(), 1U);
    EXPECT_TRUE(snapshot.display.traces.empty());
}

TEST(CommandBusTest, SerializesCommandsThatExpectTheSameRevision) {
    constexpr int commandCount = 16;
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    std::atomic<int> ready{0};
    std::promise<void> startPromise;
    const auto startSignal = startPromise.get_future().share();
    std::vector<std::future<CommandResult>> pending;

    for (int index = 0; index < commandCount; ++index) {
        pending.push_back(std::async(std::launch::async, [&, index] {
            ready.fetch_add(1);
            startSignal.wait();
            return commandBus.dispatch(makeCommand(
                "concurrent-" + std::to_string(index),
                0,
                CreateChannelCommand{validSweep()}));
        }));
    }
    while (ready.load() != commandCount) {
        std::this_thread::yield();
    }
    startPromise.set_value();

    std::size_t successCount = 0;
    for (auto& result : pending) {
        successCount += isSuccess(result.get());
    }
    EXPECT_EQ(successCount, 1U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 1U);
    EXPECT_EQ(snapshot.instrument.channels.size(), 1U);
}

}  // namespace
}  // namespace vna::application
