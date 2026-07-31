#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <utility>

#include <vna/application/command_bus.hpp>

namespace vna::application {
namespace {

TEST(CommandBusTest, SuccessfulCommandIncrementsRevisionOnce) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const CommandEnvelope command{
        .commandId = CommandId{"command-1"},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = 0,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = CreateChannelCommand{domain::SweepSettings{
            .startFrequencyHz = 1'000'000'000,
            .stopFrequencyHz = 2'000'000'000,
            .points = 201,
            .ifBandwidthHz = 1'000,
            .powerDbm = -10.0,
        }},
    };

    const auto result = commandBus.dispatch(command);

    EXPECT_EQ(result.status, CommandStatus::Succeeded);
    EXPECT_EQ(result.stateRevision, 1U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 1U);
    EXPECT_EQ(snapshot.instrument.channels.size(), 1U);
}

TEST(CommandBusTest, InvalidCommandDoesNotChangeStateOrRevision) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const CommandEnvelope command{
        .commandId = CommandId{"command-1"},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = 0,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = CreateChannelCommand{domain::SweepSettings{
            .startFrequencyHz = 2'000'000'000,
            .stopFrequencyHz = 1'000'000'000,
            .points = 201,
            .ifBandwidthHz = 1'000,
            .powerDbm = -10.0,
        }},
    };

    const auto result = commandBus.dispatch(command);

    EXPECT_EQ(result.status, CommandStatus::ValidationError);
    EXPECT_EQ(result.stateRevision, 0U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 0U);
    EXPECT_TRUE(snapshot.instrument.channels.empty());
}

TEST(CommandBusTest, StaleRevisionIsRejectedWithoutChangingState) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto sweep = domain::SweepSettings{
        .startFrequencyHz = 1'000'000'000,
        .stopFrequencyHz = 2'000'000'000,
        .points = 201,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
    };
    const CommandEnvelope firstCommand{
        .commandId = CommandId{"command-1"},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = 0,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = CreateChannelCommand{sweep},
    };
    ASSERT_EQ(
        commandBus.dispatch(firstCommand).status,
        CommandStatus::Succeeded);
    const CommandEnvelope staleCommand{
        .commandId = CommandId{"command-2"},
        .sessionId = SessionId{"session-2"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = 0,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = CreateChannelCommand{sweep},
    };

    const auto result = commandBus.dispatch(staleCommand);

    EXPECT_EQ(result.status, CommandStatus::Conflict);
    EXPECT_EQ(result.stateRevision, 1U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 1U);
    EXPECT_EQ(snapshot.instrument.channels.size(), 1U);
}

TEST(CommandBusTest, CommandForAnotherInstrumentIsRejected) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const CommandEnvelope command{
        .commandId = CommandId{"command-1"},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-2"},
        .expectedStateRevision = 0,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = CreateChannelCommand{domain::SweepSettings{
            .startFrequencyHz = 1'000'000'000,
            .stopFrequencyHz = 2'000'000'000,
            .points = 201,
            .ifBandwidthHz = 1'000,
            .powerDbm = -10.0,
        }},
    };

    const auto result = commandBus.dispatch(command);

    EXPECT_EQ(result.status, CommandStatus::WrongInstrument);
    EXPECT_EQ(result.stateRevision, 0U);
    EXPECT_TRUE(commandBus.snapshot().instrument.channels.empty());
}

TEST(CommandBusTest, CreatesMeasurementAndTraceThroughUnifiedEntryPoint) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto makeEnvelope = [](std::string commandId,
                                 std::uint64_t revision,
                                 CommandPayload payload) {
        return CommandEnvelope{
            .commandId = CommandId{std::move(commandId)},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .expectedStateRevision = revision,
            .timeout = std::chrono::seconds{5},
            .priority = CommandPriority::Normal,
            .payload = std::move(payload),
        };
    };

    const auto channelResult = commandBus.dispatch(makeEnvelope(
        "command-1",
        0,
        CreateChannelCommand{domain::SweepSettings{
            .startFrequencyHz = 1'000'000'000,
            .stopFrequencyHz = 2'000'000'000,
            .points = 201,
            .ifBandwidthHz = 1'000,
            .powerDbm = -10.0,
        }}));
    ASSERT_EQ(channelResult.status, CommandStatus::Succeeded);
    const auto channelId = std::get<domain::ChannelId>(channelResult.value);

    const auto measurementResult = commandBus.dispatch(makeEnvelope(
        "command-2",
        1,
        CreateMeasurementCommand{channelId, domain::MeasurementType::S11}));
    ASSERT_EQ(measurementResult.status, CommandStatus::Succeeded);
    const auto measurementId =
        std::get<domain::MeasurementId>(measurementResult.value);

    const auto windowResult = commandBus.dispatch(
        makeEnvelope("command-3", 2, CreateWindowCommand{}));
    ASSERT_EQ(windowResult.status, CommandStatus::Succeeded);
    const auto windowId = std::get<domain::WindowId>(windowResult.value);

    const auto traceResult = commandBus.dispatch(makeEnvelope(
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

}  // namespace
}  // namespace vna::application
