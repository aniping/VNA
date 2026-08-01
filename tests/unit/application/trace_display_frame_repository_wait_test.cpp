#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::application {
namespace {

using namespace std::chrono_literals;

TraceDisplayFrame frameFor(
    display_model::TraceId traceId,
    std::uint64_t sequence) {
    return {
        .frameId = frames::FrameId{sequence + 10},
        .traceId = traceId,
        .stateRevision = 7,
        .sequenceNumber = sequence,
        .format = display_model::TraceFormat::LogMagnitude,
        .valueUnit = display_model::ScaleUnit::Decibel,
        .frequenciesHz = {1'000'000.0, 2'000'000.0},
        .values = {-3.0, -6.0},
    };
}

class WaitCall {
public:
    WaitCall(
        const TraceDisplayFrameRepository& repository,
        display_model::TraceId traceId,
        std::uint64_t afterSequence)
        : entered_(enteredPromise_.get_future()),
          returned_(returnedPromise_.get_future()),
          worker_([this, &repository, traceId, afterSequence](
                      std::stop_token token) {
              enteredPromise_.set_value();
              result_ = repository.waitForNext(
                  traceId, afterSequence, token);
              returnedPromise_.set_value();
          }) {}

    ~WaitCall() {
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

    TraceDisplayFrameHandle finish() {
        if (returned_.wait_for(2s) != std::future_status::ready) {
            worker_.request_stop();
            throw std::runtime_error{"repository wait did not finish"};
        }
        worker_.join();
        return result_;
    }

private:
    std::promise<void> enteredPromise_;
    std::future<void> entered_;
    std::promise<void> returnedPromise_;
    std::future<void> returned_;
    TraceDisplayFrameHandle result_;
    std::jthread worker_;
};

TEST(TraceDisplayFrameRepositoryWaitTest, ReturnsNewestRetainedFrameImmediately) {
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{1}, 1))
                    .hasValue());
    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{1}, 3))
                    .hasValue());

    const auto result =
        repository.waitForNext(display_model::TraceId{1}, 1);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->sequenceNumber, 3U);
}

TEST(TraceDisplayFrameRepositoryWaitTest, WakesOnlyAfterSuccessfulPublish) {
    TraceDisplayFrameRepository repository{1};
    WaitCall wait{repository, display_model::TraceId{1}, 0};
    ASSERT_TRUE(wait.hasEntered());
    auto invalid = frameFor(display_model::TraceId{1}, 1);
    invalid.format = display_model::TraceFormat::Phase;

    EXPECT_FALSE(repository.publish(std::move(invalid)).hasValue());
    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{2}, 1))
                    .hasValue());
    EXPECT_FALSE(repository.publish(frameFor(display_model::TraceId{1}, 1))
                     .hasValue());
    EXPECT_FALSE(wait.hasReturned());
    repository.discard(display_model::TraceId{2});
    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{1}, 2))
                    .hasValue());

    const auto result = wait.finish();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->sequenceNumber, 2U);

    WaitCall replacement{repository, display_model::TraceId{1}, 2};
    ASSERT_TRUE(replacement.hasEntered());
    EXPECT_FALSE(repository.publish(frameFor(display_model::TraceId{1}, 1))
                     .hasValue());
    EXPECT_FALSE(replacement.hasReturned());
    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{1}, 3))
                    .hasValue());
    ASSERT_NE(replacement.finish(), nullptr);
}

TEST(TraceDisplayFrameRepositoryWaitTest, CancellationReturnsNull) {
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{1}, 1))
                    .hasValue());
    std::stop_source cancelled;
    cancelled.request_stop();
    EXPECT_EQ(
        repository.waitForNext(
            display_model::TraceId{1}, 0, cancelled.get_token()),
        nullptr);

    WaitCall wait{repository, display_model::TraceId{1}, 1};
    ASSERT_TRUE(wait.hasEntered());
    EXPECT_FALSE(wait.hasReturned());
    wait.requestStop();
    EXPECT_EQ(wait.finish(), nullptr);
}

TEST(TraceDisplayFrameRepositoryWaitTest, OtherTraceEventsDoNotEndWait) {
    TraceDisplayFrameRepository repository{2};
    WaitCall wait{repository, display_model::TraceId{1}, 0};
    ASSERT_TRUE(wait.hasEntered());

    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{2}, 1))
                    .hasValue());
    repository.discard(display_model::TraceId{2});
    EXPECT_FALSE(wait.hasReturned());
    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{1}, 2))
                    .hasValue());

    const auto result = wait.finish();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->traceId, display_model::TraceId{1});
}

TEST(TraceDisplayFrameRepositoryWaitTest, DiscardKeepsExistingReaderAlive) {
    TraceDisplayFrameRepository repository{1};
    const auto published =
        repository.publish(frameFor(display_model::TraceId{1}, 1));
    ASSERT_TRUE(published.hasValue());
    const auto reader = published.value();

    repository.discard(display_model::TraceId{1});

    EXPECT_EQ(repository.latest(display_model::TraceId{1}), nullptr);
    EXPECT_EQ(reader->sequenceNumber, 1U);
    EXPECT_DOUBLE_EQ(reader->values.front(), -3.0);
}

TEST(TraceDisplayFrameRepositoryWaitTest, DiscardStartRaceCannotHang) {
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{1}, 1))
                    .hasValue());
    WaitCall wait{repository, display_model::TraceId{1}, 1};
    ASSERT_TRUE(wait.hasEntered());

    repository.discard(display_model::TraceId{1});
    if (!wait.hasReturned()) {
        ASSERT_TRUE(repository.publish(frameFor(display_model::TraceId{1}, 2))
                        .hasValue());
    }

    const auto result = wait.finish();
    // The public seam cannot expose the internal registration instant. Both
    // linearizations are valid, but an old frame or an endless wait is not.
    EXPECT_TRUE(result == nullptr || result->sequenceNumber == 2U);
}

}  // namespace
}  // namespace vna::application
