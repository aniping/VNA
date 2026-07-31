#include <gtest/gtest.h>

#include <deque>
#include <utility>
#include <variant>
#include <vector>

#include <vna/application/command_bus.hpp>
#include <vna/application/single_sweep_command_handler.hpp>

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
    StartSingleSweepHarness()
        : handler_([this](SingleSweepWorkItem work) {
              return submitWork(std::move(work));
          }),
          bus_(InstrumentId{"instrument-1"}, handler_) {}

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
    void rejectNext(SingleSweepSubmitErrorCode code) {
        rejections_.push_back(code);
    }
    [[nodiscard]] const std::vector<SingleSweepWorkItem>& submitted() const {
        return submitted_;
    }

private:
    SingleSweepSubmitResult submitWork(SingleSweepWorkItem work) {
        submitted_.push_back(work);
        if (!rejections_.empty()) {
            const auto code = rejections_.front();
            rejections_.pop_front();
            return SingleSweepSubmitError{.code = code};
        }
        return OperationId{71};
    }

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

    std::vector<SingleSweepWorkItem> submitted_;
    std::deque<SingleSweepSubmitErrorCode> rejections_;
    SingleSweepCommandHandler handler_;
    CommandBus bus_;
};

TEST(StartSingleSweepCommandTest, CachesMissingSweepTargetsWithoutSubmitting) {
    StartSingleSweepHarness harness;
    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(
        harness.createChannel().outcome));

    const auto first = harness.start(domain::ChannelId{1});
    const auto replay = harness.start(domain::ChannelId{1});

    ASSERT_NE(applicationError(first), nullptr);
    EXPECT_EQ(applicationError(first)->code,
              ApplicationErrorCode::UnsupportedSweepConfiguration);
    EXPECT_EQ(first.stateRevision, 1U);
    EXPECT_EQ(replay.stateRevision, 1U);
    EXPECT_EQ(harness.bus().snapshot().stateRevision, 1U);
    EXPECT_EQ(harness.bus().stats().idempotencyEntries, 2U);
    EXPECT_TRUE(harness.submitted().empty());
}

TEST(StartSingleSweepCommandTest, AcceptsOneS11LogMagnitudeSweep) {
    StartSingleSweepHarness harness;
    const auto channel = harness.configureSupportedSweep();

    const auto result = harness.start(channel);

    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(result.outcome));
    EXPECT_EQ(successValue<OperationId>(result), OperationId{71});
    EXPECT_EQ(result.stateRevision, 4U);
    EXPECT_EQ(harness.bus().snapshot().stateRevision, 4U);
    ASSERT_EQ(harness.submitted().size(), 1U);
    const auto& work = harness.submitted().front();
    EXPECT_EQ(work.commandId, CommandId{"start-sweep"});
    EXPECT_EQ(work.sessionId, SessionId{"session-1"});
    EXPECT_EQ(work.frameContext.channelId, channel);
    EXPECT_EQ(work.frameContext.stateRevision, 4U);
    EXPECT_EQ(work.frequencyAxis.startFrequencyHz, 1'000'000U);
    EXPECT_EQ(work.frequencyAxis.stopFrequencyHz, 2'000'000U);
    EXPECT_EQ(work.frequencyAxis.points, 5U);
    EXPECT_EQ(work.measurement.id, domain::MeasurementId{1});
    EXPECT_EQ(work.measurement.type, domain::MeasurementType::S11);
    EXPECT_EQ(work.traceId, display_model::TraceId{1});
}

TEST(StartSingleSweepCommandTest, DoesNotCacheOrConsumeRejectedAdmission) {
    StartSingleSweepHarness harness;
    const auto channel = harness.configureSupportedSweep();
    harness.rejectNext(SingleSweepSubmitErrorCode::QueueFull);
    harness.rejectNext(SingleSweepSubmitErrorCode::Stopped);

    const auto full = harness.start(channel);
    const auto stopped = harness.start(channel);
    EXPECT_EQ(harness.bus().stats().idempotencyEntries, 4U);
    const auto accepted = harness.start(channel);

    ASSERT_NE(applicationError(full), nullptr);
    EXPECT_EQ(applicationError(full)->code, ApplicationErrorCode::ResourceBusy);
    ASSERT_NE(applicationError(stopped), nullptr);
    EXPECT_EQ(applicationError(stopped)->code,
              ApplicationErrorCode::ResourceBusy);
    EXPECT_EQ(full.stateRevision, 4U);
    EXPECT_EQ(stopped.stateRevision, 4U);
    EXPECT_EQ(successValue<OperationId>(accepted), OperationId{71});
    EXPECT_EQ(accepted.stateRevision, 4U);
    EXPECT_EQ(harness.bus().stats().idempotencyEntries, 5U);
    ASSERT_EQ(harness.submitted().size(), 3U);
    EXPECT_EQ(harness.submitted()[0].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(harness.submitted()[1].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(harness.submitted()[2].frameContext.frameId, frames::FrameId{1});
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

    EXPECT_EQ(successValue<OperationId>(accepted), OperationId{71});
    EXPECT_EQ(successValue<OperationId>(replay), OperationId{71});
    EXPECT_EQ(successValue<OperationId>(original), OperationId{71});
    EXPECT_EQ(accepted.stateRevision, 4U);
    EXPECT_EQ(replay.stateRevision, 4U);
    EXPECT_EQ(original.stateRevision, 4U);
    ASSERT_NE(applicationError(reuse), nullptr);
    EXPECT_EQ(applicationError(reuse)->code,
              ApplicationErrorCode::CommandIdReuse);
    EXPECT_EQ(reuse.stateRevision, 5U);
    EXPECT_EQ(harness.submitted().size(), 1U);
}

}  // namespace
}  // namespace vna::application
