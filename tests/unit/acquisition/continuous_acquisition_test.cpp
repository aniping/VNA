#include <gtest/gtest.h>

#include <vna/acquisition/continuous_acquisition.hpp>

#include "continuous_acquisition_test_support.hpp"

namespace vna::acquisition {
namespace {

using test_support::ControlledSource;
using test_support::validPayload;
using test_support::validPlan;

TEST(ContinuousAcquisitionTest, StartsAndPublishesCompleteFramesInSequence) {
    ControlledSource source;
    ContinuousAcquisition acquisition{validPlan(), source};

    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    const auto first = acquisition.waitForNext(0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->context.frameId, FrameId{1});
    EXPECT_EQ(first->context.sweepId, SweepId{1});
    EXPECT_EQ(first->context.sequenceNumber, 1U);
    EXPECT_EQ(first->frequencyAxis.id, frames::FrequencyAxisId{1});
    EXPECT_EQ(first->frequencyAxis.startFrequencyHz, 1'000'000U);
    EXPECT_EQ(first->frequencyAxis.stopFrequencyHz, 2'000'000U);
    EXPECT_EQ(first->frequencyAxis.points, 3U);
    EXPECT_EQ(first->payload, validPayload(1));

    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    const auto second = acquisition.waitForNext(1);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->context.frameId, FrameId{2});
    EXPECT_EQ(second->context.sweepId, SweepId{2});
    EXPECT_EQ(second->context.sequenceNumber, 2U);
    EXPECT_EQ(acquisition.latest(), second);
    EXPECT_EQ(first->payload, validPayload(1));

    ASSERT_TRUE(source.waitForRequest(3));
    acquisition.stop();
    const auto stopped = acquisition.snapshot();
    EXPECT_EQ(stopped.state, ContinuousAcquisitionState::Stopped);
    EXPECT_EQ(stopped.lastPublishedSequence, 2U);
    EXPECT_EQ(acquisition.latest(), second);
}

}  // namespace
}  // namespace vna::acquisition
