#include <gtest/gtest.h>

#include <functional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <vna/application/single_sweep_command_handler.hpp>

namespace vna::application {
namespace {

CapturedSingleSweep capturedSweep(
    domain::ChannelId channelId = domain::ChannelId{2},
    std::uint64_t revision = 7) {
    return CapturedSingleSweep{
        .commandId = CommandId{"command-1"},
        .sessionId = SessionId{"session-1"},
        .stateRevision = revision,
        .channel = {
            .id = channelId,
            .sweep = {
                .startFrequencyHz = 1'000'000,
                .stopFrequencyHz = 2'000'000,
                .points = 5,
                .ifBandwidthHz = 1'000,
                .powerDbm = -10.0,
            },
        },
        .measurement = {
            .id = domain::MeasurementId{3},
            .channelId = channelId,
            .type = domain::MeasurementType::S11,
        },
        .trace = {
            .id = display_model::TraceId{4},
            .windowId = display_model::WindowId{5},
            .measurementId = domain::MeasurementId{3},
            .format = display_model::TraceFormat::LogMagnitude,
        },
    };
}

OperationSnapshot acceptedOperation(
    OperationId id,
    const SingleSweepWorkItem& work) {
    return OperationSnapshot{
        .id = id,
        .commandId = work.commandId,
        .sessionId = work.sessionId,
        .submittedAtStateRevision = work.frameContext.stateRevision,
        .state = OperationQueued{},
    };
}

TEST(SingleSweepCommandHandlerTest, RejectsAnEmptySubmitPort) {
    EXPECT_THROW(
        static_cast<void>(SingleSweepCommandHandler{SingleSweepSubmit{}}),
        std::invalid_argument);
}

TEST(SingleSweepCommandHandlerTest, AssignsCorrelationAndChannelSequences) {
    std::vector<SingleSweepWorkItem> submitted;
    std::uint64_t nextOperationId = 41;
    SingleSweepCommandHandler handler{[&](SingleSweepWorkItem work) {
        submitted.push_back(work);
        return SingleSweepSubmitResult{
            acceptedOperation(OperationId{nextOperationId++}, work)};
    }};

    const auto first = handler.submit(capturedSweep());
    const auto second = handler.submit(capturedSweep(domain::ChannelId{2}, 8));
    const auto otherChannel =
        handler.submit(capturedSweep(domain::ChannelId{9}, 9));

    EXPECT_EQ(std::get<OperationId>(first), OperationId{41});
    EXPECT_EQ(std::get<OperationId>(second), OperationId{42});
    EXPECT_EQ(std::get<OperationId>(otherChannel), OperationId{43});
    ASSERT_EQ(submitted.size(), 3U);
    EXPECT_EQ(submitted[0].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(submitted[1].frameContext.frameId, frames::FrameId{2});
    EXPECT_EQ(submitted[2].frameContext.frameId, frames::FrameId{3});
    EXPECT_EQ(submitted[0].frameContext.sweepId, frames::SweepId{1});
    EXPECT_EQ(submitted[1].frequencyAxis.id, frames::FrequencyAxisId{2});
    EXPECT_EQ(submitted[0].frameContext.sequenceNumber, 1U);
    EXPECT_EQ(submitted[1].frameContext.sequenceNumber, 2U);
    EXPECT_EQ(submitted[2].frameContext.sequenceNumber, 1U);
    EXPECT_EQ(submitted[1].frameContext.stateRevision, 8U);
    EXPECT_EQ(submitted[2].frameContext.channelId, domain::ChannelId{9});
    EXPECT_EQ(submitted[0].frequencyAxis.startFrequencyHz, 1'000'000U);
    EXPECT_EQ(submitted[0].frequencyAxis.stopFrequencyHz, 2'000'000U);
    EXPECT_EQ(submitted[0].frequencyAxis.points, 5U);
    EXPECT_EQ(submitted[0].measurement.id, domain::MeasurementId{3});
    EXPECT_EQ(submitted[0].traceId, display_model::TraceId{4});
}

TEST(SingleSweepCommandHandlerTest, RejectionsDoNotConsumeCandidates) {
    std::vector<SingleSweepWorkItem> submitted;
    SingleSweepCommandHandler handler{[&](SingleSweepWorkItem work) {
        submitted.push_back(work);
        if (submitted.size() == 1U) {
            return SingleSweepSubmitResult{SingleSweepSubmitError{
                .code = SingleSweepSubmitErrorCode::QueueFull}};
        }
        if (submitted.size() == 2U) {
            return SingleSweepSubmitResult{SingleSweepSubmitError{
                .code = SingleSweepSubmitErrorCode::Stopped}};
        }
        return SingleSweepSubmitResult{
            acceptedOperation(OperationId{51}, work)};
    }};

    const auto full = handler.submit(capturedSweep());
    const auto stopped = handler.submit(capturedSweep());
    const auto accepted = handler.submit(capturedSweep());

    EXPECT_EQ(std::get<SingleSweepSubmitError>(full).code,
              SingleSweepSubmitErrorCode::QueueFull);
    EXPECT_EQ(std::get<SingleSweepSubmitError>(stopped).code,
              SingleSweepSubmitErrorCode::Stopped);
    EXPECT_EQ(std::get<OperationId>(accepted), OperationId{51});
    ASSERT_EQ(submitted.size(), 3U);
    EXPECT_EQ(submitted[0].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(submitted[1].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(submitted[2].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(submitted[2].frameContext.sweepId, frames::SweepId{1});
    EXPECT_EQ(submitted[2].frequencyAxis.id, frames::FrequencyAxisId{1});
    EXPECT_EQ(submitted[2].frameContext.sequenceNumber, 1U);
}

}  // namespace
}  // namespace vna::application
