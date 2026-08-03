#include <gtest/gtest.h>

#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

constexpr domain::SweepSettings validSweep() {
    return {
        .startFrequencyHz = 1'000'000,
        .stopFrequencyHz = 2'000'000,
        .points = 5,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
    };
}

const ApplicationError* applicationError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<ApplicationError>(error);
}

template <typename Value>
Value successValue(const CommandResult& result) {
    return std::get<Value>(std::get<CommandSuccess>(result.outcome).value);
}

class StartSingleSweepHarness {
public:
    explicit StartSingleSweepHarness(std::size_t idempotencyCapacity = 1024)
        : bus_(InstrumentId{"instrument-1"}, runtimeOwner_.runtime(),
               idempotencyCapacity) {}

    CommandResult createChannel() {
        return bus_.dispatch(envelope(
            "create-channel", CreateChannelCommand{.sweep = validSweep()}));
    }

    CommandResult start(
        domain::ChannelId channelId,
        const char* commandId = "start-sweep") {
        return bus_.dispatch(startEnvelope(channelId, commandId));
    }

    CommandEnvelope startEnvelope(
        domain::ChannelId channelId,
        const char* commandId = "start-sweep") {
        return envelope(commandId, StartSingleSweepCommand{.channelId = channelId});
    }

    CommandResult createAnotherWindow() {
        return bus_.dispatch(envelope("advance-state", CreateWindowCommand{}));
    }

    domain::ChannelId configureSupportedSweep() {
        const auto channel = successValue<domain::ChannelId>(createChannel());
        const auto measurement = successValue<domain::MeasurementId>(
            bus_.dispatch(envelope(
                "create-measurement",
                CreateMeasurementCommand{
                    .channelId = channel,
                    .type = domain::MeasurementType::S11})));
        const auto window = successValue<display_model::WindowId>(
            bus_.dispatch(envelope("create-window", CreateWindowCommand{})));
        static_cast<void>(bus_.dispatch(envelope(
            "create-trace",
            CreateTraceCommand{
                .windowId = window,
                .measurementId = measurement,
                .format = display_model::TraceFormat::LogMagnitude})));
        return channel;
    }

    [[nodiscard]] CommandBus& bus() noexcept { return bus_; }
    void stopRuntime() { runtimeOwner_.runtime().stop(); }

private:
    CommandEnvelope envelope(const char* commandId, CommandPayload payload) {
        return {
            .commandId = CommandId{commandId},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .origin = CommandOrigin::Web,
            .expectedStateRevision = bus_.snapshot().stateRevision,
            .payload = std::move(payload),
        };
    }

    vna::test::CommandBusRuntimeOwner runtimeOwner_;
    CommandBus bus_;
};

TEST(StartSingleSweepCommandTest, AcceptsChannelWithoutLegacyTraceShape) {
    StartSingleSweepHarness harness;
    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(
        harness.createChannel().outcome));

    const auto first = harness.start(domain::ChannelId{1});
    const auto replay = harness.start(domain::ChannelId{1});

    EXPECT_EQ(successValue<OperationId>(first),
              successValue<OperationId>(replay));
    EXPECT_EQ(first.stateRevision, 1U);
    EXPECT_EQ(replay.stateRevision, 1U);
    EXPECT_EQ(harness.bus().snapshot().stateRevision, 1U);
    EXPECT_EQ(harness.bus().stats().idempotencyEntries, 2U);
}

TEST(StartSingleSweepCommandTest, AcceptsOneS11LogMagnitudeSweep) {
    StartSingleSweepHarness harness;
    const auto channel = harness.configureSupportedSweep();

    const auto result = harness.start(channel);

    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(result.outcome));
    EXPECT_GT(successValue<OperationId>(result).value(), 0U);
    EXPECT_EQ(result.stateRevision, 4U);
    EXPECT_EQ(harness.bus().snapshot().stateRevision, 4U);
}

TEST(StartSingleSweepCommandTest, DoesNotCacheUnavailableRuntime) {
    StartSingleSweepHarness harness{4};
    const auto channel = harness.configureSupportedSweep();
    harness.stopRuntime();

    const auto first = harness.start(channel);
    const auto retried = harness.start(channel);
    EXPECT_EQ(harness.bus().stats().idempotencyEntries, 4U);
    EXPECT_EQ(harness.bus().stats().idempotencyEvictions, 0U);

    ASSERT_NE(applicationError(first), nullptr);
    EXPECT_EQ(applicationError(first)->code, ApplicationErrorCode::ResourceBusy);
    ASSERT_NE(applicationError(retried), nullptr);
    EXPECT_EQ(applicationError(retried)->code,
              ApplicationErrorCode::ResourceBusy);
    EXPECT_EQ(first.stateRevision, 4U);
    EXPECT_EQ(retried.stateRevision, 4U);
}

TEST(StartSingleSweepCommandTest, ReplaysAcceptedOperationAndRejectsChannelReuse) {
    StartSingleSweepHarness harness;
    const auto channel = harness.configureSupportedSweep();
    const auto command = harness.startEnvelope(channel);

    const auto accepted = harness.bus().dispatch(command);
    const auto replay = harness.bus().dispatch(command);
    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(
        harness.createAnotherWindow().outcome));
    auto reused = command;
    reused.payload = StartSingleSweepCommand{.channelId = domain::ChannelId{99}};
    const auto reuse = harness.bus().dispatch(reused);
    const auto original = harness.bus().dispatch(command);

    EXPECT_EQ(successValue<OperationId>(accepted),
              successValue<OperationId>(replay));
    EXPECT_EQ(successValue<OperationId>(accepted),
              successValue<OperationId>(original));
    EXPECT_EQ(accepted.stateRevision, 4U);
    EXPECT_EQ(replay.stateRevision, 4U);
    EXPECT_EQ(original.stateRevision, 4U);
    ASSERT_NE(applicationError(reuse), nullptr);
    EXPECT_EQ(applicationError(reuse)->code,
              ApplicationErrorCode::CommandIdReuse);
    EXPECT_EQ(reuse.stateRevision, 5U);
}

}  // namespace
}  // namespace vna::application
