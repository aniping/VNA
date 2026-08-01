#include <gtest/gtest.h>

#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

class StartGate {
public:
    explicit StartGate(std::size_t participants) : participants_(participants) {}

    void arriveAndWait() {
        std::unique_lock lock{mutex_};
        ++arrived_;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
    }

    void releaseWhenReady() {
        std::unique_lock lock{mutex_};
        condition_.wait(
            lock, [this] { return arrived_ == participants_; });
        released_ = true;
        condition_.notify_all();
    }

private:
    std::size_t participants_;
    std::size_t arrived_{0};
    bool released_{false};
    std::mutex mutex_;
    std::condition_variable condition_;
};

CommandEnvelope command(
    const char* commandId,
    std::uint64_t revision,
    CommandPayload payload) {
    return {
        .commandId = CommandId{commandId},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .origin = CommandOrigin::Web,
        .expectedStateRevision = revision,
        .payload = std::move(payload),
    };
}

CommandEnvelope windowCommand(const char* commandId, std::uint64_t revision = 0) {
    return command(commandId, revision, CreateWindowCommand{});
}
const CommandSuccess* success(const CommandResult& result) {
    return std::get_if<CommandSuccess>(&result.outcome);
}
const ApplicationError* applicationError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<ApplicationError>(error);
}

TEST(CommandIdempotencyHardeningTest, ConcurrentSameKeyExecutesOnce) {
    constexpr std::size_t requestCount = 8;
    vna::test::StoppedCommandBus commandBus{InstrumentId{"instrument-1"}};
    const auto command = windowCommand("shared-command");
    StartGate gate{requestCount};
    std::vector<std::optional<CommandResult>> results(requestCount);
    std::vector<std::thread> threads;
    threads.reserve(requestCount);
    for (std::size_t index = 0; index < requestCount; ++index) {
        threads.emplace_back([&, index] {
            gate.arriveAndWait();
            results[index] = commandBus.dispatch(command);
        });
    }

    gate.releaseWhenReady();
    for (auto& thread : threads) {
        thread.join();
    }

    for (const auto& result : results) {
        ASSERT_TRUE(result.has_value());
        ASSERT_NE(success(*result), nullptr);
        EXPECT_EQ(result->stateRevision, 1U);
        EXPECT_EQ(
            success(*result)->value,
            CommandValue{display_model::WindowId{1}});
    }
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 1U);
    EXPECT_EQ(snapshot.display.windows.size(), 1U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 1U);
}

constexpr domain::SweepSettings validSweep() {
    return {
        .startFrequencyHz = 10'000'000,
        .stopFrequencyHz = 26'500'000'000,
        .points = 201,
        .ifBandwidthHz = 10'000,
        .powerDbm = -10.0,
    };
}

class ScaleIdempotencyHardeningTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_NE(
            success(commandBus_.dispatch(command(
                "create-channel", 0, CreateChannelCommand{validSweep()}))),
            nullptr);
        ASSERT_NE(
            success(commandBus_.dispatch(command(
                "create-measurement",
                1,
                CreateMeasurementCommand{
                    domain::ChannelId{1}, domain::MeasurementType::S11}))),
            nullptr);
        ASSERT_NE(
            success(commandBus_.dispatch(windowCommand("create-window", 2))),
            nullptr);
        const auto trace = commandBus_.dispatch(command(
            "create-trace",
            3,
            CreateTraceCommand{
                display_model::WindowId{1},
                domain::MeasurementId{1},
                display_model::TraceFormat::LogMagnitude}));
        ASSERT_NE(success(trace), nullptr);
        traceId_ = std::get<display_model::TraceId>(success(trace)->value);
    }

    CommandEnvelope scaleCommand(const char* commandId, double scale) const {
        return command(
            commandId,
            4,
            UpdateTraceScalePerDivisionCommand{traceId_, scale});
    }

    vna::test::StoppedCommandBus commandBus_{InstrumentId{"instrument-1"}};
    display_model::TraceId traceId_{0};
};

TEST_F(ScaleIdempotencyHardeningTest, ReplaysSameScaleAndRejectsChangedScale) {
    const auto first = commandBus_.dispatch(scaleCommand("scale", 5.0));
    const auto replay = commandBus_.dispatch(scaleCommand("scale", 5.0));
    const auto reused = commandBus_.dispatch(scaleCommand("scale", 6.0));
    ASSERT_NE(success(first), nullptr);
    ASSERT_NE(success(replay), nullptr);
    EXPECT_EQ(replay.stateRevision, first.stateRevision);
    EXPECT_EQ(success(replay)->value, success(first)->value);
    const auto* reusedError = std::get_if<CommandError>(&reused.outcome);
    ASSERT_NE(reusedError, nullptr);
    EXPECT_EQ(commandErrorCode(*reusedError), CommandErrorCode::CommandIdReuse);
    EXPECT_EQ(reused.stateRevision, 5U);
    const auto snapshot = commandBus_.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 5U);
    ASSERT_EQ(snapshot.display.traces.size(), 1U);
    EXPECT_DOUBLE_EQ(snapshot.display.traces[0].scale->scalePerDivision, 5.0);
}

TEST_F(ScaleIdempotencyHardeningTest, DistinguishesZeroSignAndReplaysNan) {
    const auto positiveZero =
        commandBus_.dispatch(scaleCommand("signed-zero", +0.0));
    const auto negativeZero =
        commandBus_.dispatch(scaleCommand("signed-zero", -0.0));
    const auto* zeroError = std::get_if<CommandError>(&positiveZero.outcome);
    ASSERT_NE(zeroError, nullptr);
    EXPECT_EQ(commandErrorCode(*zeroError), CommandErrorCode::InvalidScalePerDivision);
    EXPECT_EQ(positiveZero.stateRevision, 4U);
    ASSERT_NE(applicationError(negativeZero), nullptr);
    EXPECT_EQ(applicationError(negativeZero)->code, ApplicationErrorCode::CommandIdReuse);
    EXPECT_EQ(negativeZero.stateRevision, 4U);
    const auto afterZero = commandBus_.snapshot();
    EXPECT_EQ(afterZero.stateRevision, 4U);
    EXPECT_DOUBLE_EQ(afterZero.display.traces[0].scale->scalePerDivision, 10.0);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = scaleCommand("nan-scale", nan);
    const auto first = commandBus_.dispatch(invalid);
    ASSERT_NE(
        success(commandBus_.dispatch(windowCommand("advance", 4))),
        nullptr);

    const auto replay = commandBus_.dispatch(invalid);
    const auto* error = std::get_if<CommandError>(&replay.outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(
        commandErrorCode(*error),
        CommandErrorCode::InvalidScalePerDivision);
    EXPECT_EQ(replay.stateRevision, first.stateRevision);
    EXPECT_EQ(replay.stateRevision, 4U);
}

TEST(CommandIdempotencyHardeningTest, TransientPathsDoNotRefreshFullFifo) {
    vna::test::StoppedCommandBus commandBus{InstrumentId{"instrument-1"}, 2};
    const auto oldest = windowCommand("oldest", 0);
    const auto newest = windowCommand("newest", 1);
    ASSERT_NE(success(commandBus.dispatch(oldest)), nullptr);
    ASSERT_NE(success(commandBus.dispatch(newest)), nullptr);

    const auto replay = commandBus.dispatch(oldest);
    const auto reused = commandBus.dispatch(windowCommand("oldest", 2));
    auto wrong = oldest;
    wrong.instrumentId = InstrumentId{"instrument-2"};
    const auto rejected = commandBus.dispatch(wrong);

    ASSERT_NE(success(replay), nullptr);
    ASSERT_NE(applicationError(reused), nullptr);
    EXPECT_EQ(
        applicationError(reused)->code,
        ApplicationErrorCode::CommandIdReuse);
    ASSERT_NE(applicationError(rejected), nullptr);
    EXPECT_EQ(
        applicationError(rejected)->code,
        ApplicationErrorCode::WrongInstrument);
    const auto unchanged = commandBus.stats();
    EXPECT_EQ(unchanged.idempotencyEntries, 2U);
    EXPECT_EQ(unchanged.idempotencyEvictions, 0U);

    ASSERT_NE(
        success(commandBus.dispatch(windowCommand("incoming", 2))),
        nullptr);
    const auto afterIncoming = commandBus.stats();
    EXPECT_EQ(afterIncoming.idempotencyEntries, 2U);
    EXPECT_EQ(afterIncoming.idempotencyEvictions, 1U);
    const auto newestReplay = commandBus.dispatch(newest);
    ASSERT_NE(success(newestReplay), nullptr);
    EXPECT_EQ(newestReplay.stateRevision, 2U);

    const auto evictedRetry = commandBus.dispatch(oldest);
    ASSERT_NE(applicationError(evictedRetry), nullptr);
    EXPECT_EQ(
        applicationError(evictedRetry)->code,
        ApplicationErrorCode::StateRevisionConflict);
    EXPECT_EQ(evictedRetry.stateRevision, 3U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 2U);
    EXPECT_EQ(commandBus.stats().idempotencyEvictions, 2U);
}

}  // namespace
}  // namespace vna::application
