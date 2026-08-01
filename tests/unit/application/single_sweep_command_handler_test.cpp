#include <gtest/gtest.h>

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <vna/application/command_bus.hpp>
#include <vna/application/single_sweep_command_handler.hpp>

namespace vna::application {
namespace {

static_assert(!std::is_constructible_v<CommandBus, InstrumentId>);
static_assert(std::is_constructible_v<
              CommandBus,
              InstrumentId,
              SingleSweepCommandHandler&,
              TracePublicationCatalog&>);
static_assert(std::is_constructible_v<
              SingleSweepCommandHandler, SingleSweepExecution&>);
static_assert(noexcept(std::declval<SingleSweepCommandHandler&>().discard(
    display_model::TraceId{1})));
static_assert(noexcept(
    std::declval<SingleSweepCommandHandler&>().invalidateFrame(
        display_model::TraceId{1})));

class RecordingExecution final : public SingleSweepExecution {
public:
    using Behavior = std::function<SingleSweepSubmitResult(
        const SingleSweepWorkItem&,
        std::size_t)>;

    explicit RecordingExecution(Behavior behavior)
        : behavior_(std::move(behavior)) {}

    SingleSweepSubmitResult submit(SingleSweepWorkItem work) override {
        submitted.push_back(std::move(work));
        return behavior_(submitted.back(), submitted.size());
    }

    void discardTrace(display_model::TraceId traceId) noexcept override {
        discarded = traceId;
    }
    void invalidateTraceFrame(
        display_model::TraceId traceId) noexcept override {
        invalidated = traceId;
    }

    std::vector<SingleSweepWorkItem> submitted;
    display_model::TraceId discarded{0};
    display_model::TraceId invalidated{0};

private:
    Behavior behavior_;
};

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

TEST(SingleSweepCommandHandlerTest, AssignsCorrelationAndChannelSequences) {
    std::uint64_t nextOperationId = 41;
    RecordingExecution execution{[&](const auto&, std::size_t) {
        return SingleSweepSubmitResult{OperationId{nextOperationId++}};
    }};
    SingleSweepCommandHandler handler{execution};

    const auto first = handler.submit(capturedSweep());
    const auto second = handler.submit(capturedSweep(domain::ChannelId{2}, 8));
    const auto otherChannel =
        handler.submit(capturedSweep(domain::ChannelId{9}, 9));

    EXPECT_EQ(std::get<OperationId>(first), OperationId{41});
    EXPECT_EQ(std::get<OperationId>(second), OperationId{42});
    EXPECT_EQ(std::get<OperationId>(otherChannel), OperationId{43});
    ASSERT_EQ(execution.submitted.size(), 3U);
    EXPECT_EQ(execution.submitted[0].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(execution.submitted[1].frameContext.frameId, frames::FrameId{2});
    EXPECT_EQ(execution.submitted[2].frameContext.frameId, frames::FrameId{3});
    EXPECT_EQ(execution.submitted[0].frameContext.sweepId, frames::SweepId{1});
    EXPECT_EQ(execution.submitted[1].frequencyAxis.id,
              frames::FrequencyAxisId{2});
    EXPECT_EQ(execution.submitted[0].frameContext.sequenceNumber, 1U);
    EXPECT_EQ(execution.submitted[1].frameContext.sequenceNumber, 2U);
    EXPECT_EQ(execution.submitted[2].frameContext.sequenceNumber, 1U);
    EXPECT_EQ(execution.submitted[1].frameContext.stateRevision, 8U);
    EXPECT_EQ(execution.submitted[2].frameContext.channelId,
              domain::ChannelId{9});
    EXPECT_EQ(execution.submitted[0].frequencyAxis.startFrequencyHz,
              1'000'000U);
    EXPECT_EQ(execution.submitted[0].frequencyAxis.stopFrequencyHz,
              2'000'000U);
    EXPECT_EQ(execution.submitted[0].frequencyAxis.points, 5U);
    EXPECT_EQ(execution.submitted[0].measurement.id,
              domain::MeasurementId{3});
    EXPECT_EQ(execution.submitted[0].traceId, display_model::TraceId{4});
}

TEST(SingleSweepCommandHandlerTest, RejectionsDoNotConsumeCandidates) {
    RecordingExecution execution{[](const auto&, std::size_t attempt) {
        if (attempt == 1U) {
            return SingleSweepSubmitResult{SingleSweepSubmitError{
                .code = SingleSweepSubmitErrorCode::QueueFull}};
        }
        if (attempt == 2U) {
            return SingleSweepSubmitResult{SingleSweepSubmitError{
                .code = SingleSweepSubmitErrorCode::Stopped}};
        }
        return SingleSweepSubmitResult{OperationId{51}};
    }};
    SingleSweepCommandHandler handler{execution};

    const auto full = handler.submit(capturedSweep());
    const auto stopped = handler.submit(capturedSweep());
    const auto accepted = handler.submit(capturedSweep());

    EXPECT_EQ(std::get<SingleSweepSubmitError>(full).code,
              SingleSweepSubmitErrorCode::QueueFull);
    EXPECT_EQ(std::get<SingleSweepSubmitError>(stopped).code,
              SingleSweepSubmitErrorCode::Stopped);
    EXPECT_EQ(std::get<OperationId>(accepted), OperationId{51});
    ASSERT_EQ(execution.submitted.size(), 3U);
    EXPECT_EQ(execution.submitted[0].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(execution.submitted[1].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(execution.submitted[2].frameContext.frameId, frames::FrameId{1});
    EXPECT_EQ(execution.submitted[2].frameContext.sweepId, frames::SweepId{1});
    EXPECT_EQ(execution.submitted[2].frequencyAxis.id,
              frames::FrequencyAxisId{1});
    EXPECT_EQ(execution.submitted[2].frameContext.sequenceNumber, 1U);
}

TEST(SingleSweepCommandHandlerTest, DelegatesRetirementToExecutionOwner) {
    RecordingExecution execution{[](const auto&, std::size_t) {
        return SingleSweepSubmitResult{OperationId{1}};
    }};
    SingleSweepCommandHandler handler{execution};

    handler.discard(display_model::TraceId{7});

    EXPECT_EQ(execution.discarded, display_model::TraceId{7});
}

TEST(SingleSweepCommandHandlerTest, DelegatesFrameInvalidationWithoutRetirement) {
    RecordingExecution execution{[](const auto&, std::size_t) {
        return SingleSweepSubmitResult{OperationId{1}};
    }};
    SingleSweepCommandHandler handler{execution};

    handler.invalidateFrame(display_model::TraceId{8});

    EXPECT_EQ(execution.invalidated, display_model::TraceId{8});
    EXPECT_EQ(execution.discarded, display_model::TraceId{0});
}

}  // namespace
}  // namespace vna::application
