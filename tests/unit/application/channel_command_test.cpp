#include <gtest/gtest.h>

#include <utility>

#include <vna/test/stopped_single_sweep_handler.hpp>

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
        .origin = CommandOrigin::Web,
        .expectedStateRevision = revision,
        .payload = std::move(payload),
    };
}

TEST(ChannelCommandTest, UpdatesExistingChannelSweep) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    const domain::SweepSettings initial{
        .startFrequencyHz = 10'000'000,
        .stopFrequencyHz = 26'500'000'000,
        .points = 201,
        .ifBandwidthHz = 10'000,
        .powerDbm = -10.0,
    };
    const auto created = commandBus.dispatch(
        command("create-channel", 0, CreateChannelCommand{initial}));
    ASSERT_TRUE(isSuccess(created));

    const domain::SweepSettings updated{
        .startFrequencyHz = 100'000'000,
        .stopFrequencyHz = 3'000'000'000,
        .points = 401,
        .ifBandwidthHz = 1'000,
        .powerDbm = -5.0,
    };
    const auto result = commandBus.dispatch(command(
        "update-sweep",
        1,
        UpdateChannelSweepCommand{domain::ChannelId{1}, updated}));

    EXPECT_TRUE(isSuccess(result));
    EXPECT_EQ(result.stateRevision, 2U);
    const auto snapshot = commandBus.snapshot();
    ASSERT_EQ(snapshot.instrument.channels.size(), 1U);
    EXPECT_EQ(snapshot.instrument.channels[0].sweep.startFrequencyHz, 100'000'000U);
    EXPECT_EQ(snapshot.instrument.channels[0].sweep.stopFrequencyHz, 3'000'000'000U);
    EXPECT_EQ(snapshot.instrument.channels[0].sweep.points, 401U);
    EXPECT_EQ(snapshot.instrument.channels[0].sweep.ifBandwidthHz, 1'000U);
    EXPECT_DOUBLE_EQ(snapshot.instrument.channels[0].sweep.powerDbm, -5.0);
}

TEST(ChannelCommandTest, InvalidUpdateKeepsChannelAndRevisionUnchanged) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    const domain::SweepSettings initial{
        .startFrequencyHz = 10'000'000,
        .stopFrequencyHz = 26'500'000'000,
        .points = 201,
        .ifBandwidthHz = 10'000,
        .powerDbm = -10.0,
    };
    ASSERT_TRUE(isSuccess(commandBus.dispatch(
        command("create-channel", 0, CreateChannelCommand{initial}))));
    auto invalid = initial;
    invalid.startFrequencyHz = 30'000'000'000;

    const auto result = commandBus.dispatch(command(
        "invalid-update",
        1,
        UpdateChannelSweepCommand{domain::ChannelId{1}, invalid}));

    EXPECT_TRUE(std::holds_alternative<CommandError>(result.outcome));
    EXPECT_EQ(result.stateRevision, 1U);
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.instrument.channels[0].sweep.startFrequencyHz, 10'000'000U);
    EXPECT_EQ(snapshot.instrument.channels[0].sweep.stopFrequencyHz, 26'500'000'000U);
}

TEST(ChannelCommandTest, RejectsUpdateForMissingChannel) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    const domain::SweepSettings sweep{
        .startFrequencyHz = 10'000'000,
        .stopFrequencyHz = 26'500'000'000,
        .points = 201,
        .ifBandwidthHz = 10'000,
        .powerDbm = -10.0,
    };

    const auto result = commandBus.dispatch(command(
        "missing-channel",
        0,
        UpdateChannelSweepCommand{domain::ChannelId{99}, sweep}));

    EXPECT_TRUE(std::holds_alternative<CommandError>(result.outcome));
    EXPECT_EQ(result.stateRevision, 0U);
    EXPECT_TRUE(commandBus.snapshot().instrument.channels.empty());
}

}  // namespace
}  // namespace vna::application
