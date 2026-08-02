#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <utility>
#include <variant>
#include <vector>

#include <vna/acquisition/raw_sweep_capture.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::acquisition {
namespace {

using test_support::validPayload;
using test_support::validPlan;

RawSweepCaptureRequest request(std::uint32_t maximumPointsPerChunk = 2) {
    return {
        .plan = validPlan(),
        .sweepId = SweepId{41},
        .sequenceNumber = 7,
        .maximumPointsPerChunk = maximumPointsPerChunk,
    };
}

RawSweepChunkProducer payloadProducer(std::vector<RawSweepChunkRequest>& seen) {
    return [&seen](const ContinuousAcquisitionPlan&,
                   RawSweepChunkRequest chunk,
                   std::stop_token) -> RawSweepChunkResult {
        seen.push_back(chunk);
        const auto payload = validPayload(chunk.sequenceNumber);
        const auto& source = payload.sourceStates.at(chunk.sourcePort - 1);
        const auto first = source.samples.cbegin() + chunk.firstPoint;
        return RawSweepPointRange{
            .sourcePort = chunk.sourcePort,
            .firstPoint = chunk.firstPoint,
            .samples = {first, first + chunk.pointCount},
        };
    };
}

TEST(RawSweepCaptureTest, EmitsOrderedRangesThenReturnsCompletePayload) {
    std::vector<RawSweepChunkRequest> requests;
    std::vector<RawSweepPointRange> observed;
    auto ordered = request();
    ordered.plan.sourcePorts = {2, 1};

    const auto result = captureRawSweep(
        ordered, payloadProducer(requests),
        [&observed](const auto& chunk) { observed.push_back(chunk); });

    const auto* payload = std::get_if<frames::RawReceiverPayload>(&result);
    ASSERT_NE(payload, nullptr);
    auto expected = validPayload(7);
    std::swap(expected.sourceStates[0], expected.sourceStates[1]);
    EXPECT_EQ(*payload, expected);
    ASSERT_EQ(requests.size(), 4U);
    EXPECT_EQ(requests[0], (RawSweepChunkRequest{SweepId{41}, 7, 2, 0, 2}));
    EXPECT_EQ(requests[1], (RawSweepChunkRequest{SweepId{41}, 7, 2, 2, 1}));
    EXPECT_EQ(requests[2], (RawSweepChunkRequest{SweepId{41}, 7, 1, 0, 2}));
    EXPECT_EQ(requests[3], (RawSweepChunkRequest{SweepId{41}, 7, 1, 2, 1}));
    ASSERT_EQ(observed.size(), requests.size());
    for (std::size_t index = 0; index < observed.size(); ++index) {
        EXPECT_EQ(observed[index].sourcePort, requests[index].sourcePort);
        EXPECT_EQ(observed[index].firstPoint, requests[index].firstPoint);
        EXPECT_EQ(observed[index].samples.size(), requests[index].pointCount);
    }
}

TEST(RawSweepCaptureTest, RejectsMismatchedRangeWithoutPublishingPartial) {
    int observed = 0;
    RawSweepChunkProducer producer = [](
        const ContinuousAcquisitionPlan&,
        RawSweepChunkRequest chunk,
        std::stop_token) -> RawSweepChunkResult {
        auto payload = validPayload(chunk.sequenceNumber);
        return RawSweepPointRange{
            .sourcePort = chunk.sourcePort + 1,
            .firstPoint = chunk.firstPoint,
            .samples = {payload.sourceStates[0].samples[0]},
        };
    };

    const auto result = captureRawSweep(
        request(), producer, [&observed](const auto&) { ++observed; });

    const auto* error = std::get_if<frames::FrameError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, frames::FrameErrorCode::InvalidSourcePort);
    EXPECT_EQ(observed, 0);
}

TEST(RawSweepCaptureTest, CancellationAfterAChunkNeverReturnsCompletePayload) {
    std::vector<RawSweepChunkRequest> requests;
    std::stop_source stop;
    int observed = 0;

    const auto result = captureRawSweep(
        request(), payloadProducer(requests),
        [&](const auto&) {
            ++observed;
            stop.request_stop();
        },
        stop.get_token());

    EXPECT_TRUE(std::holds_alternative<RawSweepCaptureCanceled>(result));
    EXPECT_EQ(observed, 1);
    EXPECT_EQ(requests.size(), 1U);
}

TEST(RawSweepCaptureTest, CancellationAtFinalRangeStillRejectsCompletePayload) {
    std::vector<RawSweepChunkRequest> requests;
    std::stop_source stop;
    int observed = 0;

    const auto result = captureRawSweep(
        request(), payloadProducer(requests),
        [&](const auto&) {
            if (++observed == 4) {
                stop.request_stop();
            }
        },
        stop.get_token());

    EXPECT_TRUE(std::holds_alternative<RawSweepCaptureCanceled>(result));
    EXPECT_EQ(observed, 4);
    EXPECT_EQ(requests.size(), 4U);
}

TEST(RawSweepCaptureTest, SourceFailureDoesNotNotifyOrReturnPayload) {
    int observed = 0;
    RawSweepChunkProducer producer = [](
        const ContinuousAcquisitionPlan&,
        RawSweepChunkRequest,
        std::stop_token) -> RawSweepChunkResult {
        return frames::FrameError{frames::FrameErrorCode::NonFiniteSample};
    };

    const auto result = captureRawSweep(
        request(), producer, [&observed](const auto&) { ++observed; });

    const auto* error = std::get_if<frames::FrameError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, frames::FrameErrorCode::NonFiniteSample);
    EXPECT_EQ(observed, 0);
}

TEST(RawSweepCaptureTest, RejectsNegativePlanPeriodBeforeCallingSource) {
    auto invalid = request();
    invalid.plan.minimumSweepPeriod = -std::chrono::milliseconds{1};
    int calls = 0;
    RawSweepChunkProducer producer = [&calls](
        const ContinuousAcquisitionPlan&,
        RawSweepChunkRequest,
        std::stop_token) -> RawSweepChunkResult {
        ++calls;
        return frames::FrameError{frames::FrameErrorCode::InvalidFrequencyAxis};
    };

    const auto result = captureRawSweep(invalid, producer);

    const auto* error = std::get_if<frames::FrameError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, frames::FrameErrorCode::InvalidAcquisitionSettings);
    EXPECT_EQ(calls, 0);
}

}  // namespace
}  // namespace vna::acquisition
