#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>

#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/application/disabled_single_sweep_execution.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

#include "single_sweep_executor_test_support.hpp"

namespace vna::application {
namespace {
using namespace std::chrono_literals;

ContinuousTracePreset retirementPreset() {
    return {
        .stateRevision = 7,
        .measurement = {
            domain::MeasurementId{1}, domain::ChannelId{1},
            domain::MeasurementType::S21},
        .trace = {
            display_model::TraceId{1}, display_model::WindowId{1},
            domain::MeasurementId{1},
            display_model::TraceFormat::LogMagnitude, std::nullopt},
    };
}

TraceDisplayFrame seedFrame() {
    return {
        .frameId = frames::FrameId{91},
        .traceId = display_model::TraceId{1},
        .measurementId = domain::MeasurementId{1},
        .measurementType = domain::MeasurementType::S21,
        .stateRevision = 6,
        .generation = 1,
        .sequenceNumber = 9,
        .format = display_model::TraceFormat::LogMagnitude,
        .frequenciesHz = {1'000'000, 2'000'000},
        .samples = CartesianTraceDisplaySamples{
            .unit = TraceDisplayUnit::Decibel,
            .values = {-3.0, -2.0}},
    };
}

class BlockingPublisher {
public:
    explicit BlockingPublisher(TraceDisplayFrameRepository& repository)
        : repository_(repository), state_(std::make_shared<State>()) {}
    TraceDisplayFrameResult operator()(TraceDisplayFrame frame) const {
        {
            std::unique_lock lock{state_->mutex};
            ++state_->calls;
            state_->entered = true;
            state_->changed.notify_all();
            state_->changed.wait(lock, [&] { return state_->released; });
        }
        return repository_.publish(std::move(frame));
    }
    bool waitUntilEntered() const {
        std::unique_lock lock{state_->mutex};
        return state_->changed.wait_for(lock, 2s, [&] {
            return state_->entered;
        });
    }
    void release() const {
        std::lock_guard lock{state_->mutex};
        state_->released = true;
        state_->changed.notify_all();
    }
    std::size_t calls() const {
        std::lock_guard lock{state_->mutex};
        return state_->calls;
    }
private:
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered{}, released{};
        std::size_t calls{};
    };
    TraceDisplayFrameRepository& repository_;
    std::shared_ptr<State> state_;
};

TEST(ContinuousTraceRetirementTest, RetireWaitsForPublishingThenDiscards) {
    acquisition::test_support::ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    BlockingPublisher probe{repository};
    ContinuousTracePublisher publisher{
        acquisition, retirementPreset(), repository, probe};
    const auto firstRequested = source.waitForRequest(1);
    source.release(1);
    const auto publishEntered = probe.waitUntilEntered();
    std::promise<void> retireStarted;
    auto retireStartedFuture = retireStarted.get_future();
    auto retirement = std::async(std::launch::async, [&] {
        retireStarted.set_value();
        publisher.retireTrace(display_model::TraceId{1});
    });
    const auto retireWasScheduled =
        retireStartedFuture.wait_for(2s) == std::future_status::ready;
    const auto waitedForPublish =
        retirement.wait_for(0ms) == std::future_status::timeout;

    probe.release();
    retirement.get();
    const auto secondRequested = source.waitForRequest(2);
    source.release(2);
    const auto thirdRequested = source.waitForRequest(3);
    publisher.stop();
    const auto acquisitionState = acquisition.snapshot().state;
    acquisition.stop();

    EXPECT_TRUE(firstRequested);
    EXPECT_TRUE(publishEntered);
    EXPECT_TRUE(retireWasScheduled);
    EXPECT_TRUE(waitedForPublish);
    EXPECT_TRUE(secondRequested);
    EXPECT_TRUE(thirdRequested);
    EXPECT_EQ(probe.calls(), 1U);
    EXPECT_EQ(publisher.snapshot().state,
              ContinuousTracePublisherState::Retired);
    EXPECT_EQ(repository.latest(display_model::TraceId{1}), nullptr);
    EXPECT_EQ(acquisitionState, acquisition::ContinuousAcquisitionState::Running);
}

TEST(ContinuousTraceRetirementTest, RetireBeforePublishIsIdempotentAndScoped) {
    acquisition::test_support::ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(repository.publish(seedFrame()).hasValue());
    const auto oldHandle = repository.latest(display_model::TraceId{1});
    ContinuousTracePublisher publisher{
        acquisition, retirementPreset(), repository};
    const auto firstRequested = source.waitForRequest(1);

    publisher.retireTrace(display_model::TraceId{2});
    const auto ignoredState = publisher.snapshot().state;
    publisher.retireTrace(display_model::TraceId{1});
    publisher.retireTrace(display_model::TraceId{1});
    source.release(1);
    const auto secondRequested = source.waitForRequest(2);
    publisher.stop();
    const auto acquisitionState = acquisition.snapshot().state;
    acquisition.stop();

    EXPECT_TRUE(firstRequested);
    EXPECT_TRUE(secondRequested);
    EXPECT_EQ(ignoredState, ContinuousTracePublisherState::Running);
    EXPECT_EQ(publisher.snapshot().state,
              ContinuousTracePublisherState::Retired);
    EXPECT_EQ(repository.latest(display_model::TraceId{1}), nullptr);
    ASSERT_NE(oldHandle, nullptr);
    EXPECT_EQ(oldHandle->sequenceNumber, 9U);
    EXPECT_EQ(acquisitionState, acquisition::ContinuousAcquisitionState::Running);
}

TEST(DisabledSingleSweepExecutionTest, RejectsSubmitAndForwardsRetirement) {
    acquisition::test_support::ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(repository.publish(seedFrame()).hasValue());
    ContinuousTracePublisher publisher{
        acquisition, retirementPreset(), repository};
    DisabledSingleSweepExecution disabled{publisher};
    const auto firstRequested = source.waitForRequest(1);

    const auto result = disabled.submit(test_support::validWorkItem());
    disabled.invalidateTraceFrame(display_model::TraceId{1});
    const auto stateAfterInvalidation = publisher.snapshot().state;
    const auto frameAfterInvalidation = repository.latest(
        display_model::TraceId{1});
    ASSERT_TRUE(repository.publish(seedFrame()).hasValue());
    disabled.discardTrace(display_model::TraceId{1});
    publisher.stop();
    const auto acquisitionSnapshot = acquisition.snapshot();
    acquisition.stop();

    const auto* error = std::get_if<SingleSweepSubmitError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, SingleSweepSubmitErrorCode::Stopped);
    EXPECT_TRUE(firstRequested);
    EXPECT_EQ(stateAfterInvalidation, ContinuousTracePublisherState::Running);
    EXPECT_EQ(frameAfterInvalidation, nullptr);
    EXPECT_EQ(acquisitionSnapshot.state,
              acquisition::ContinuousAcquisitionState::Running);
    EXPECT_EQ(acquisitionSnapshot.lastPublishedSequence, 0U);
    EXPECT_EQ(repository.latest(display_model::TraceId{1}), nullptr);
    EXPECT_EQ(publisher.snapshot().state,
              ContinuousTracePublisherState::Retired);
}

}  // namespace
}  // namespace vna::application
