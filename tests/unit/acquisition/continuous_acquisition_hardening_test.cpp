#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <future>
#include <mutex>
#include <stdexcept>
#include <vna/compat/stop_token.hpp>
#include <utility>
#include <variant>

#include <vna/acquisition/continuous_acquisition.hpp>

#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::acquisition {
namespace {

using namespace std::chrono_literals;
using test_support::ControlledSource;
using test_support::validPayload;
using test_support::validPlan;

const ContinuousAcquisitionFailure* requireFailure(
    const ContinuousAcquisitionSnapshot& snapshot,
    ContinuousAcquisitionFailureCode expected) {
    EXPECT_EQ(snapshot.state, ContinuousAcquisitionState::Failed);
    if (!snapshot.failure.has_value()) {
        ADD_FAILURE() << "missing acquisition failure";
        return nullptr;
    }
    EXPECT_EQ(snapshot.failure->code, expected);
    return &*snapshot.failure;
}

TEST(ContinuousAcquisitionHardeningTest, SourceErrorFailsOnceAndSurvivesStop) {
    std::atomic<int> calls{0};
    RawSweepSource source = [&](
                                const auto&,
                                std::uint64_t,
                                vna::compat::StopToken) {
        ++calls;
        return frames::Result<frames::RawReceiverPayload>{frames::FrameError{
            frames::FrameErrorCode::InvalidFrequencyAxis}};
    };
    ContinuousAcquisition acquisition{validPlan(), std::move(source)};

    acquisition.join();
    acquisition.stop();

    const auto snapshot = acquisition.snapshot();
    const auto* failure = requireFailure(
        snapshot, ContinuousAcquisitionFailureCode::SourceFailed);
    ASSERT_NE(failure, nullptr);
    EXPECT_EQ(failure->attemptedSequence, 1U);
    const auto* cause = std::get_if<frames::FrameError>(&failure->cause);
    ASSERT_NE(cause, nullptr);
    EXPECT_EQ(cause->code, frames::FrameErrorCode::InvalidFrequencyAxis);
    EXPECT_EQ(snapshot.lastPublishedSequence, 0U);
    EXPECT_EQ(acquisition.latest(), nullptr);
    EXPECT_EQ(calls.load(), 1);
}

TEST(ContinuousAcquisitionHardeningTest, InvalidSecondFrameIsNotPublished) {
    std::atomic<int> calls{0};
    RawSweepSource source = [&](const auto&, std::uint64_t sequence,
                                vna::compat::StopToken) {
        auto payload = validPayload(sequence);
        if (++calls == 2) {
            payload.sourceStates.front().samples.pop_back();
        }
        return frames::Result<frames::RawReceiverPayload>{std::move(payload)};
    };
    ContinuousAcquisition acquisition{validPlan(), std::move(source)};

    acquisition.join();

    const auto snapshot = acquisition.snapshot();
    const auto* failure = requireFailure(
        snapshot, ContinuousAcquisitionFailureCode::RawFrameRejected);
    ASSERT_NE(failure, nullptr);
    EXPECT_EQ(failure->attemptedSequence, 2U);
    const auto* cause = std::get_if<frames::FrameError>(&failure->cause);
    ASSERT_NE(cause, nullptr);
    EXPECT_EQ(cause->code, frames::FrameErrorCode::SampleCountMismatch);
    ASSERT_NE(acquisition.latest(), nullptr);
    EXPECT_EQ(acquisition.latest()->context.sequenceNumber, 1U);
    EXPECT_EQ(acquisition.latest()->payload, validPayload(1));
    EXPECT_EQ(calls.load(), 2);
}

TEST(ContinuousAcquisitionHardeningTest, SourceExceptionIsContained) {
    RawSweepSource source = [](
                                const auto&,
                                std::uint64_t,
                                vna::compat::StopToken)
        -> frames::Result<frames::RawReceiverPayload> {
        throw std::runtime_error{"receiver disconnected"};
    };
    ContinuousAcquisition acquisition{validPlan(), std::move(source)};

    acquisition.join();

    const auto snapshot = acquisition.snapshot();
    const auto* failure = requireFailure(
        snapshot,
        ContinuousAcquisitionFailureCode::UnexpectedFailure);
    ASSERT_NE(failure, nullptr);
    const auto* cause = std::get_if<std::exception_ptr>(&failure->cause);
    ASSERT_NE(cause, nullptr);
    EXPECT_THROW(std::rethrow_exception(*cause), std::runtime_error);
    EXPECT_EQ(acquisition.latest(), nullptr);
}

TEST(ContinuousAcquisitionHardeningTest, StopWinsBeforeBlockedSourceError) {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::mutex mutex;
    std::condition_variable changed;
    bool stopObserved = false;
    RawSweepSource source = [&](
                                const auto&,
                                std::uint64_t,
                                vna::compat::StopToken token) {
        vna::compat::StopCallback notify{token, [&] {
            std::lock_guard lock{mutex};
            stopObserved = true;
            changed.notify_all();
        }};
        entered.set_value();
        std::unique_lock lock{mutex};
        changed.wait(lock, [&] { return stopObserved; });
        return frames::Result<frames::RawReceiverPayload>{frames::FrameError{
            frames::FrameErrorCode::InvalidFrequencyAxis}};
    };
    ContinuousAcquisition acquisition{validPlan(), std::move(source)};
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);

    acquisition.stop();

    const auto snapshot = acquisition.snapshot();
    EXPECT_EQ(snapshot.state, ContinuousAcquisitionState::Stopped);
    EXPECT_FALSE(snapshot.failure.has_value());
    EXPECT_EQ(snapshot.lastPublishedSequence, 0U);
}

TEST(ContinuousAcquisitionHardeningTest, SlowConsumerReceivesLatestFrame) {
    ControlledSource source;
    ContinuousAcquisition acquisition{validPlan(), source};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    const auto first = acquisition.waitForNext(0);
    ASSERT_NE(first, nullptr);

    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    ASSERT_TRUE(source.waitForRequest(3));
    source.release(3);
    ASSERT_TRUE(source.waitForRequest(4));

    const auto latest = acquisition.waitForNext(1);
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->context.sequenceNumber, 3U);
    EXPECT_EQ(first->context.sequenceNumber, 1U);
    EXPECT_EQ(first->payload, validPayload(1));
    acquisition.stop();
}

TEST(ContinuousAcquisitionHardeningTest, StopWakesMinimumPeriodWait) {
    ControlledSource source;
    auto plan = validPlan();
    plan.minimumSweepPeriod = std::chrono::hours{1};
    ContinuousAcquisition acquisition{std::move(plan), source};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    const auto first = acquisition.waitForNext(0);
    ASSERT_NE(first, nullptr);

    acquisition.stop();

    const auto snapshot = acquisition.snapshot();
    EXPECT_EQ(snapshot.state, ContinuousAcquisitionState::Stopped);
    EXPECT_EQ(snapshot.lastPublishedSequence, 1U);
}

}  // namespace
}  // namespace vna::acquisition
