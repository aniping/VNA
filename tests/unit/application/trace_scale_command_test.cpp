#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <utility>

#include <vna/application/command_bus.hpp>

namespace vna::application {
namespace {

bool isSuccess(const CommandResult& result) {
    return std::holds_alternative<CommandSuccess>(result.outcome);
}

const display_model::DisplayError* displayError(
    const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr
        ? nullptr
        : std::get_if<display_model::DisplayError>(error);
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

CommandEnvelope command(
    const char* id,
    std::uint64_t revision,
    CommandPayload payload) {
    return {
        .commandId = CommandId{id},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = revision,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = std::move(payload),
    };
}

class TraceScaleCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto channel = commandBus_.dispatch(command(
            "create-channel", 0, CreateChannelCommand{validSweep()}));
        ASSERT_TRUE(isSuccess(channel));
        const auto measurement = commandBus_.dispatch(command(
            "create-measurement",
            1,
            CreateMeasurementCommand{
                domain::ChannelId{1},
                domain::MeasurementType::S11}));
        ASSERT_TRUE(isSuccess(measurement));
        const auto window = commandBus_.dispatch(
            command("create-window", 2, CreateWindowCommand{}));
        ASSERT_TRUE(isSuccess(window));
        revision_ = 3;
    }

    display_model::TraceId createTrace(display_model::TraceFormat format) {
        const auto result = commandBus_.dispatch(command(
            "create-trace",
            revision_,
            CreateTraceCommand{
                display_model::WindowId{1},
                domain::MeasurementId{1},
                format}));
        EXPECT_TRUE(isSuccess(result));
        revision_ = result.stateRevision;
        return std::get<display_model::TraceId>(
            std::get<CommandSuccess>(result.outcome).value);
    }

    CommandResult updateScale(
        display_model::TraceId traceId,
        double scalePerDivision) {
        return commandBus_.dispatch(command(
            "update-scale",
            revision_,
            UpdateTraceScalePerDivisionCommand{
                traceId,
                scalePerDivision}));
    }

    CommandBus commandBus_{InstrumentId{"instrument-1"}};
    std::uint64_t revision_{0};
};

TEST_F(TraceScaleCommandTest, UpdatesScaleAndIncrementsRevisionOnce) {
    const auto traceId = createTrace(
        display_model::TraceFormat::LogMagnitude);

    const auto result = updateScale(traceId, 5.0);

    ASSERT_TRUE(isSuccess(result));
    EXPECT_EQ(result.stateRevision, revision_ + 1);
    EXPECT_EQ(
        std::get<display_model::TraceId>(
            std::get<CommandSuccess>(result.outcome).value),
        traceId);
    const auto snapshot = commandBus_.snapshot();
    EXPECT_EQ(snapshot.stateRevision, revision_ + 1);
    ASSERT_EQ(snapshot.display.traces.size(), 1U);
    const auto& scale = snapshot.display.traces[0].scale.value();
    EXPECT_DOUBLE_EQ(scale.scalePerDivision, 5.0);
    EXPECT_DOUBLE_EQ(scale.minimum, -40.0);
    EXPECT_DOUBLE_EQ(scale.maximum, 10.0);
}

TEST_F(TraceScaleCommandTest, RejectsMissingTraceWithoutChangingState) {
    createTrace(display_model::TraceFormat::LogMagnitude);
    const auto before = commandBus_.snapshot();
    ASSERT_EQ(before.display.traces.size(), 1U);

    const auto result = updateScale(display_model::TraceId{99}, 5.0);

    ASSERT_NE(displayError(result), nullptr);
    EXPECT_EQ(
        displayError(result)->code,
        display_model::DisplayErrorCode::TraceNotFound);
    EXPECT_EQ(result.stateRevision, before.stateRevision);
    const auto after = commandBus_.snapshot();
    EXPECT_EQ(after.stateRevision, before.stateRevision);
    ASSERT_EQ(after.display.traces.size(), before.display.traces.size());
    const auto& beforeTrace = before.display.traces[0];
    const auto& afterTrace = after.display.traces[0];
    EXPECT_EQ(afterTrace.id, beforeTrace.id);
    EXPECT_EQ(afterTrace.windowId, beforeTrace.windowId);
    EXPECT_EQ(afterTrace.measurementId, beforeTrace.measurementId);
    EXPECT_EQ(afterTrace.format, beforeTrace.format);
    ASSERT_TRUE(beforeTrace.scale.has_value());
    ASSERT_TRUE(afterTrace.scale.has_value());
    const auto& beforeScale = beforeTrace.scale.value();
    const auto& afterScale = afterTrace.scale.value();
    EXPECT_DOUBLE_EQ(afterScale.scalePerDivision, beforeScale.scalePerDivision);
    EXPECT_DOUBLE_EQ(afterScale.referenceValue, beforeScale.referenceValue);
    EXPECT_DOUBLE_EQ(
        afterScale.referencePosition,
        beforeScale.referencePosition);
    EXPECT_DOUBLE_EQ(afterScale.minimum, beforeScale.minimum);
    EXPECT_DOUBLE_EQ(afterScale.maximum, beforeScale.maximum);
    EXPECT_EQ(afterScale.unit, beforeScale.unit);
}

class InvalidTraceScaleCommandTest
    : public TraceScaleCommandTest,
      public ::testing::WithParamInterface<double> {};

TEST_P(
    InvalidTraceScaleCommandTest,
    RejectsInvalidScaleWithoutChangingAnyTrace) {
    const auto target = createTrace(
        display_model::TraceFormat::LogMagnitude);
    const auto other = createTrace(
        display_model::TraceFormat::LogMagnitude);

    const auto result = updateScale(target, GetParam());

    ASSERT_NE(displayError(result), nullptr);
    EXPECT_EQ(
        displayError(result)->code,
        display_model::DisplayErrorCode::InvalidScalePerDivision);
    EXPECT_EQ(result.stateRevision, revision_);
    const auto snapshot = commandBus_.snapshot();
    EXPECT_EQ(snapshot.stateRevision, revision_);
    ASSERT_EQ(snapshot.display.traces.size(), 2U);
    EXPECT_EQ(snapshot.display.traces[0].id, target);
    EXPECT_EQ(snapshot.display.traces[1].id, other);
    EXPECT_DOUBLE_EQ(
        snapshot.display.traces[0].scale->scalePerDivision,
        10.0);
    EXPECT_DOUBLE_EQ(
        snapshot.display.traces[1].scale->scalePerDivision,
        10.0);
}

INSTANTIATE_TEST_SUITE_P(
    InvalidAndOverflowingValues,
    InvalidTraceScaleCommandTest,
    ::testing::Values(0.0, std::numeric_limits<double>::max()));

class UnsupportedTraceScaleCommandTest
    : public TraceScaleCommandTest,
      public ::testing::WithParamInterface<display_model::TraceFormat> {};

TEST_P(
    UnsupportedTraceScaleCommandTest,
    RejectsUnsupportedFormatWithoutChangingAnyTrace) {
    const auto target = createTrace(GetParam());
    const auto other = createTrace(
        display_model::TraceFormat::LogMagnitude);

    const auto result = updateScale(target, 5.0);

    ASSERT_NE(displayError(result), nullptr);
    EXPECT_EQ(
        displayError(result)->code,
        display_model::DisplayErrorCode::ScaleNotSupportedForFormat);
    EXPECT_EQ(result.stateRevision, revision_);
    const auto snapshot = commandBus_.snapshot();
    EXPECT_EQ(snapshot.stateRevision, revision_);
    ASSERT_EQ(snapshot.display.traces.size(), 2U);
    EXPECT_EQ(snapshot.display.traces[0].id, target);
    EXPECT_FALSE(snapshot.display.traces[0].scale.has_value());
    EXPECT_EQ(snapshot.display.traces[1].id, other);
    EXPECT_DOUBLE_EQ(
        snapshot.display.traces[1].scale->scalePerDivision,
        10.0);
}

INSTANTIATE_TEST_SUITE_P(
    UnsupportedFormats,
    UnsupportedTraceScaleCommandTest,
    ::testing::Values(
        display_model::TraceFormat::Phase,
        display_model::TraceFormat::Smith));

TEST_F(TraceScaleCommandTest, UpdatesOnlySelectedTrace) {
    const auto target = createTrace(
        display_model::TraceFormat::LogMagnitude);
    const auto other = createTrace(
        display_model::TraceFormat::LogMagnitude);

    const auto result = updateScale(target, 5.0);

    ASSERT_TRUE(isSuccess(result));
    EXPECT_EQ(result.stateRevision, revision_ + 1);
    const auto snapshot = commandBus_.snapshot();
    ASSERT_EQ(snapshot.display.traces.size(), 2U);
    EXPECT_EQ(snapshot.display.traces[0].id, target);
    EXPECT_DOUBLE_EQ(
        snapshot.display.traces[0].scale->scalePerDivision,
        5.0);
    EXPECT_EQ(snapshot.display.traces[1].id, other);
    EXPECT_DOUBLE_EQ(
        snapshot.display.traces[1].scale->scalePerDivision,
        10.0);
}

}  // namespace
}  // namespace vna::application
