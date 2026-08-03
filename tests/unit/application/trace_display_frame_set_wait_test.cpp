#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <vna/compat/joining_thread.hpp>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::application {
namespace {

using namespace std::chrono_literals;

TraceDisplayFrameSet setFor(
    std::uint64_t sequence,
    std::uint64_t generation = 1) {
    TraceDisplayFrame frame{
        .frameId = frames::FrameId{100 + sequence},
        .traceId = display_model::TraceId{1},
        .measurementId = domain::MeasurementId{1},
        .measurementType = domain::MeasurementType::S21,
        .stateRevision = 7,
        .generation = generation,
        .sequenceNumber = sequence,
        .format = display_model::TraceFormat::LogMagnitude,
        .frequenciesHz = {1.0e6, 2.0e6},
        .samples = CartesianTraceDisplaySamples{
            .unit = TraceDisplayUnit::Decibel,
            .values = {-3.0, -6.0}},
    };
    return {generation, sequence, {std::move(frame)}};
}

class SetWaitCall {
public:
    SetWaitCall(
        const TraceDisplayFrameRepository& repository,
        TraceDisplayFrameSetCursor cursor)
        : entered_(enteredPromise_.get_future()),
          returned_(returnedPromise_.get_future()),
          worker_([this, &repository, cursor](vna::compat::StopToken token) {
              enteredPromise_.set_value();
              result_ = repository.waitForNextSet(cursor, token);
              returnedPromise_.set_value();
          }) {}

    ~SetWaitCall() {
        worker_.requestStop();
    }

    [[nodiscard]] bool hasEntered() {
        return entered_.wait_for(2s) == std::future_status::ready;
    }

    [[nodiscard]] bool hasReturned() {
        return returned_.wait_for(0ms) == std::future_status::ready;
    }

    void requestStop() {
        worker_.requestStop();
    }

    std::optional<TraceDisplayFrameSetEvent> finish() {
        if (returned_.wait_for(2s) != std::future_status::ready) {
            worker_.requestStop();
            throw std::runtime_error{"frame-set wait did not finish"};
        }
        worker_.join();
        return result_;
    }

private:
    std::promise<void> enteredPromise_;
    std::future<void> entered_;
    std::promise<void> returnedPromise_;
    std::future<void> returned_;
    std::optional<TraceDisplayFrameSetEvent> result_;
    vna::compat::JoiningThread worker_;
};

FrameSetAvailable expectAvailable(
    const std::optional<TraceDisplayFrameSetEvent>& result) {
    EXPECT_TRUE(result.has_value());
    const auto* available = result
        ? std::get_if<FrameSetAvailable>(&*result)
        : nullptr;
    EXPECT_NE(available, nullptr);
    return available == nullptr ? FrameSetAvailable{} : *available;
}

TEST(TraceDisplayFrameSetWaitTest, RetainedReadSkipsToLatestCompleteSet) {
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(setFor(1))));
    ASSERT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(setFor(2))));
    ASSERT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(setFor(3))));

    const auto& available = expectAvailable(
        repository.waitForNextSet({.generation = 1, .sequenceNumber = 1}));

    ASSERT_NE(available.frameSet, nullptr);
    EXPECT_EQ(available.frameSet->sequenceNumber, 3U);
}

TEST(TraceDisplayFrameSetWaitTest, GenerationAdvanceIsAnEventBeforeNewFrame) {
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(setFor(1))));
    SetWaitCall generationWait{repository, {1, 1}};
    ASSERT_TRUE(generationWait.hasEntered());

    ASSERT_TRUE(std::holds_alternative<GenerationAdvanced>(
        repository.advanceGeneration(2)));
    const auto advanced = generationWait.finish();

    ASSERT_TRUE(advanced.has_value());
    EXPECT_EQ(std::get<GenerationAdvanced>(*advanced).generation, 2U);
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
    EXPECT_EQ(repository.latest(display_model::TraceId{1}), nullptr);
    SetWaitCall frameWait{repository, {2, 0}};
    ASSERT_TRUE(frameWait.hasEntered());
    EXPECT_FALSE(frameWait.hasReturned());
    ASSERT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(setFor(5, 2))));
    const auto& available = expectAvailable(frameWait.finish());
    ASSERT_NE(available.frameSet, nullptr);
    EXPECT_EQ(available.frameSet->sequenceNumber, 5U);
}

TEST(TraceDisplayFrameSetWaitTest, CancellationReturnsEmpty) {
    TraceDisplayFrameRepository repository{1};
    vna::compat::StopSource stopped;
    stopped.requestStop();
    EXPECT_EQ(
        repository.waitForNextSet({1, 0}, stopped.getToken()), std::nullopt);

    SetWaitCall wait{repository, {1, 0}};
    ASSERT_TRUE(wait.hasEntered());
    EXPECT_FALSE(wait.hasReturned());
    wait.requestStop();
    EXPECT_EQ(wait.finish(), std::nullopt);
}

TEST(TraceDisplayFrameSetWaitTest, FailedPublishDoesNotWakeOrReplace) {
    TraceDisplayFrameRepository repository{1};
    SetWaitCall wait{repository, {1, 0}};
    ASSERT_TRUE(wait.hasEntered());
    auto invalid = setFor(1);
    invalid.frames.front().samples = CartesianTraceDisplaySamples{
        .unit = TraceDisplayUnit::Degree,
        .values = {1.0, 2.0}};

    EXPECT_TRUE(std::holds_alternative<TraceDisplayFrameSetError>(
        repository.publishFrameSet(std::move(invalid))));
    EXPECT_FALSE(wait.hasReturned());
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
    ASSERT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(setFor(2))));
    const auto& available = expectAvailable(wait.finish());
    ASSERT_NE(available.frameSet, nullptr);
    EXPECT_EQ(available.frameSet->sequenceNumber, 2U);
}

TEST(TraceDisplayFrameSetWaitTest, PublishAtWaitRegistrationCannotBeLost) {
    for (std::uint64_t sequence = 1; sequence <= 50; ++sequence) {
        TraceDisplayFrameRepository repository{1};
        SetWaitCall wait{repository, {1, 0}};
        ASSERT_TRUE(wait.hasEntered());
        ASSERT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
            repository.publishFrameSet(setFor(sequence))));

        const auto result = wait.finish();

        ASSERT_NE(expectAvailable(result).frameSet, nullptr);
        EXPECT_EQ(expectAvailable(result).frameSet->sequenceNumber, sequence);
    }
}

}  // namespace
}  // namespace vna::application
