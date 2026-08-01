#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/single_sweep_command_handler.hpp>
#include <vna/application/trace_display_frame_query.hpp>

namespace vna::application {
namespace {

using namespace std::chrono_literals;

constexpr domain::SweepSettings sweep() {
    return {1'000'000, 2'000'000, 3, 1'000, -10.0};
}

class QueryWaitCall {
public:
    QueryWaitCall(
        const TraceDisplayFrameQuery& query,
        display_model::TraceId traceId,
        std::uint64_t afterSequence)
        : entered_(enteredPromise_.get_future()),
          returned_(returnedPromise_.get_future()),
          worker_([this, &query, traceId, afterSequence](
                      std::stop_token token) {
              enteredPromise_.set_value();
              outcome_ = query.waitForNext(traceId, afterSequence, token);
              returnedPromise_.set_value();
          }) {}

    ~QueryWaitCall() {
        worker_.request_stop();
    }

    [[nodiscard]] bool hasEntered() {
        return entered_.wait_for(2s) == std::future_status::ready;
    }

    [[nodiscard]] bool hasReturned() {
        return returned_.wait_for(0ms) == std::future_status::ready;
    }

    void requestStop() {
        worker_.request_stop();
    }

    TraceDisplayFrameQueryOutcome finish() {
        if (returned_.wait_for(2s) != std::future_status::ready) {
            worker_.request_stop();
            throw std::runtime_error{"display frame query did not finish"};
        }
        worker_.join();
        return outcome_;
    }

private:
    std::promise<void> enteredPromise_;
    std::future<void> entered_;
    std::promise<void> returnedPromise_;
    std::future<void> returned_;
    TraceDisplayFrameQueryOutcome outcome_;
    std::jthread worker_;
};

class TraceDisplayFrameQueryWaitTest
    : public ::testing::Test,
      private SingleSweepExecution {
protected:
    TraceDisplayFrameQueryWaitTest()
        : handler_(*this),
          bus_(InstrumentId{"instrument-1"}, handler_),
          query_(bus_, repository_) {}

    void SetUp() override {
        EXPECT_TRUE(success(dispatch(CreateChannelCommand{sweep()})));
        EXPECT_TRUE(success(dispatch(CreateMeasurementCommand{
            domain::ChannelId{1}, domain::MeasurementType::S11})));
        EXPECT_TRUE(success(dispatch(CreateWindowCommand{})));
        EXPECT_TRUE(success(dispatch(CreateTraceCommand{
            display_model::WindowId{1},
            domain::MeasurementId{1},
            display_model::TraceFormat::LogMagnitude})));
    }

    CommandResult dispatch(CommandPayload payload) {
        return bus_.dispatch(CommandEnvelope{
            .commandId = CommandId{"query-wait-" + std::to_string(nextId_++)},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .origin = CommandOrigin::Web,
            .expectedStateRevision = std::nullopt,
            .payload = std::move(payload),
        });
    }

    static bool success(const CommandResult& result) {
        return std::holds_alternative<CommandSuccess>(result.outcome);
    }

    TraceDisplayFrame frame(std::uint64_t sequence) {
        return {
            .frameId = frames::FrameId{sequence + 10},
            .traceId = display_model::TraceId{1},
            .measurementId = domain::MeasurementId{1},
            .measurementType = domain::MeasurementType::S11,
            .stateRevision = 3,
            .generation = 1,
            .sequenceNumber = sequence,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = {1'000'000.0, 2'000'000.0},
            .samples = CartesianTraceDisplaySamples{
                .unit = TraceDisplayUnit::Decibel,
                .values = {-3.0, -6.0}},
        };
    }

private:
    SingleSweepSubmitResult submit(SingleSweepWorkItem) override {
        return SingleSweepSubmitError{SingleSweepSubmitErrorCode::Stopped};
    }

    void invalidateTraceFrame(
        display_model::TraceId traceId) noexcept override {
        repository_.discard(traceId);
    }

    void discardTrace(display_model::TraceId traceId) noexcept override {
        repository_.discard(traceId);
    }

protected:
    TraceDisplayFrameRepository repository_{1};
    SingleSweepCommandHandler handler_;
    CommandBus bus_;
    TraceDisplayFrameQuery query_;
    std::uint64_t nextId_{1};
};

const TraceDisplayFrameHandle* frameFrom(
    const TraceDisplayFrameQueryOutcome& outcome) {
    return std::get_if<TraceDisplayFrameHandle>(&outcome);
}

void expectError(
    const TraceDisplayFrameQueryOutcome& outcome,
    TraceDisplayFrameQueryErrorCode code) {
    const auto* error = std::get_if<TraceDisplayFrameQueryError>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, code);
}

TEST_F(TraceDisplayFrameQueryWaitTest, ReturnsImmediateAndLaterFrames) {
    ASSERT_TRUE(repository_.publish(frame(1)).hasValue());
    const auto immediate =
        query_.waitForNext(display_model::TraceId{1}, 0);
    const auto* first = frameFrom(immediate);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ((*first)->sequenceNumber, 1U);
    EXPECT_EQ((*first)->stateRevision, 3U);

    QueryWaitCall wait{query_, display_model::TraceId{1}, 1};
    ASSERT_TRUE(wait.hasEntered());
    ASSERT_TRUE(repository_.publish(frame(3)).hasValue());
    const auto later = wait.finish();
    const auto* latest = frameFrom(later);
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ((*latest)->sequenceNumber, 3U);
}

TEST_F(TraceDisplayFrameQueryWaitTest, MapsCancellationAndDiscard) {
    std::stop_source cancelled;
    cancelled.request_stop();
    expectError(
        query_.waitForNext(
            display_model::TraceId{1}, 0, cancelled.get_token()),
        TraceDisplayFrameQueryErrorCode::FrameNotAvailable);

    QueryWaitCall wait{query_, display_model::TraceId{1}, 0};
    ASSERT_TRUE(wait.hasEntered());
    repository_.discard(display_model::TraceId{1});
    if (!wait.hasReturned()) {
        wait.requestStop();
    }
    expectError(
        wait.finish(), TraceDisplayFrameQueryErrorCode::FrameNotAvailable);
}

TEST_F(TraceDisplayFrameQueryWaitTest, RevalidatesDeletionAndFormatChange) {
    QueryWaitCall removed{query_, display_model::TraceId{1}, 0};
    ASSERT_TRUE(removed.hasEntered());
    ASSERT_TRUE(success(dispatch(RemoveTraceCommand{display_model::TraceId{1}})));
    expectError(removed.finish(), TraceDisplayFrameQueryErrorCode::TraceNotFound);

    ASSERT_TRUE(success(dispatch(CreateTraceCommand{
        display_model::WindowId{1},
        domain::MeasurementId{1},
        display_model::TraceFormat::LogMagnitude})));
    QueryWaitCall changed{query_, display_model::TraceId{2}, 0};
    ASSERT_TRUE(changed.hasEntered());
    ASSERT_TRUE(success(dispatch(UpdateTraceFormatCommand{
        display_model::TraceId{2}, display_model::TraceFormat::Phase})));
    expectError(changed.finish(), TraceDisplayFrameQueryErrorCode::FrameNotAvailable);
}

TEST_F(TraceDisplayFrameQueryWaitTest, SupportsCommandBusCallbackReentry) {
    const SessionId owner{"query-wait-owner"};
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    TraceDisplayFrameHandle callbackFrame;
    bool detached = false;
    ASSERT_TRUE(std::holds_alternative<ControlSnapshot>(
        bus_.tryAttachScpiSession(owner, [&] {
            entered.set_value();
            const auto outcome =
                query_.waitForNext(display_model::TraceId{1}, 0);
            if (const auto* value = frameFrom(outcome)) {
                callbackFrame = *value;
            }
            detached = std::holds_alternative<ControlSnapshot>(
                bus_.detachScpiSession(owner).outcome);
        }).outcome));
    ASSERT_TRUE(std::holds_alternative<ControlSnapshot>(
        bus_.activateScpiControl(owner).outcome));

    auto takeover = std::async(std::launch::async, [&] {
        return bus_.takeLocalControl();
    });
    EXPECT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(repository_.publish(frame(1)).hasValue());

    ASSERT_EQ(takeover.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(std::holds_alternative<ControlSnapshot>(takeover.get().outcome));
    EXPECT_NE(callbackFrame, nullptr);
    EXPECT_TRUE(detached);
}

}  // namespace
}  // namespace vna::application
