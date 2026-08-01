#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <variant>

#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

constexpr domain::SweepSettings validSweep() {
    return {
        .startFrequencyHz = 1'000'000,
        .stopFrequencyHz = 2'000'000,
        .points = 3,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
    };
}

CommandEnvelope command(
    std::string commandId,
    std::uint64_t revision,
    CommandPayload payload) {
    return {
        .commandId = CommandId{std::move(commandId)},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .origin = CommandOrigin::Web,
        .expectedStateRevision = revision,
        .payload = std::move(payload),
    };
}

bool isSuccess(const CommandResult& result) {
    return std::holds_alternative<CommandSuccess>(result.outcome);
}

TraceDisplayFrame validFrame(
    display_model::TraceId traceId,
    std::uint64_t stateRevision = 4) {
    return {
        .frameId = frames::FrameId{11},
        .traceId = traceId,
        .measurementId = domain::MeasurementId{1},
        .measurementType = domain::MeasurementType::S11,
        .stateRevision = stateRevision,
        .generation = 1,
        .sequenceNumber = 1,
        .format = display_model::TraceFormat::LogMagnitude,
        .frequenciesHz = {1'000'000.0, 1'500'000.0, 2'000'000.0},
        .samples = CartesianTraceDisplaySamples{
            .unit = TraceDisplayUnit::Decibel,
            .values = {-6.0, -12.0, -3.0}},
    };
}

class TraceDisplayFrameQueryTest : public ::testing::Test {
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
        const auto trace = commandBus_.dispatch(command(
            "create-trace",
            3,
            CreateTraceCommand{
                display_model::WindowId{1},
                domain::MeasurementId{1},
                display_model::TraceFormat::LogMagnitude}));
        ASSERT_TRUE(isSuccess(trace));
    }

    vna::test::StoppedCommandBus commandBus_{
        InstrumentId{"instrument-1"}};
    TraceDisplayFrameRepository repository_{1};
    TraceDisplayFrameQuery query_{commandBus_, repository_};
};

TEST_F(TraceDisplayFrameQueryTest, DistinguishesMissingTraceFromMissingFrame) {
    const auto outcome = query_.latest(display_model::TraceId{99});

    const auto* error =
        std::get_if<TraceDisplayFrameQueryError>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, TraceDisplayFrameQueryErrorCode::TraceNotFound);
}

TEST_F(TraceDisplayFrameQueryTest, ReportsUnavailableWhenTraceHasNoFrame) {
    const auto outcome = query_.latest(display_model::TraceId{1});

    const auto* error =
        std::get_if<TraceDisplayFrameQueryError>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(
        error->code,
        TraceDisplayFrameQueryErrorCode::FrameNotAvailable);
}

TEST_F(TraceDisplayFrameQueryTest, ReturnsLatestFrameForCurrentTraceFormat) {
    const auto published =
        repository_.publish(validFrame(display_model::TraceId{1}));
    ASSERT_TRUE(published.hasValue());

    const auto outcome = query_.latest(display_model::TraceId{1});

    const auto* frame = std::get_if<TraceDisplayFrameHandle>(&outcome);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(*frame, published.value());
    EXPECT_EQ((*frame)->traceId, display_model::TraceId{1});
    EXPECT_EQ((*frame)->stateRevision, 4U);
}

TEST_F(TraceDisplayFrameQueryTest, HidesFrameAfterTraceFormatChanges) {
    const auto published =
        repository_.publish(validFrame(display_model::TraceId{1}));
    ASSERT_TRUE(published.hasValue());
    const auto updated = commandBus_.dispatch(command(
        "update-format",
        4,
        UpdateTraceFormatCommand{
            display_model::TraceId{1},
            display_model::TraceFormat::Phase}));
    ASSERT_TRUE(isSuccess(updated));

    const auto outcome = query_.latest(display_model::TraceId{1});

    const auto* error =
        std::get_if<TraceDisplayFrameQueryError>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(
        error->code,
        TraceDisplayFrameQueryErrorCode::FrameNotAvailable);
}

TEST_F(TraceDisplayFrameQueryTest, KeepsFrameAcrossScaleAndRevisionChanges) {
    const auto published =
        repository_.publish(validFrame(display_model::TraceId{1}, 4));
    ASSERT_TRUE(published.hasValue());
    const auto updated = commandBus_.dispatch(command(
        "update-scale",
        4,
        UpdateTraceScalePerDivisionCommand{
            display_model::TraceId{1},
            5.0}));
    ASSERT_TRUE(isSuccess(updated));
    ASSERT_EQ(updated.stateRevision, 5U);

    const auto outcome = query_.latest(display_model::TraceId{1});

    const auto* frame = std::get_if<TraceDisplayFrameHandle>(&outcome);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(*frame, published.value());
    EXPECT_EQ((*frame)->stateRevision, 4U);
    const auto current = commandBus_.snapshot();
    EXPECT_EQ(current.stateRevision, 5U);
    ASSERT_TRUE(current.display.traces[0].scale.has_value());
    EXPECT_DOUBLE_EQ(
        current.display.traces[0].scale->scalePerDivision,
        5.0);
}

TEST_F(TraceDisplayFrameQueryTest, SupportsReentryFromCommandBusCallback) {
    const auto published =
        repository_.publish(validFrame(display_model::TraceId{1}));
    ASSERT_TRUE(published.hasValue());
    const SessionId owner{"scpi-query-owner"};
    TraceDisplayFrameHandle callbackFrame;
    bool detached = false;
    std::size_t callbackCount = 0;
    const auto attached = commandBus_.tryAttachScpiSession(owner, [&] {
        ++callbackCount;
        const auto outcome = query_.latest(display_model::TraceId{1});
        if (const auto* frame =
                std::get_if<TraceDisplayFrameHandle>(&outcome)) {
            callbackFrame = *frame;
        }
        const auto result = commandBus_.detachScpiSession(owner);
        detached = std::holds_alternative<ControlSnapshot>(result.outcome);
    });
    ASSERT_TRUE(std::holds_alternative<ControlSnapshot>(attached.outcome));
    const auto activated = commandBus_.activateScpiControl(owner);
    ASSERT_TRUE(std::holds_alternative<ControlSnapshot>(activated.outcome));

    const auto takeover = commandBus_.takeLocalControl();

    ASSERT_TRUE(std::holds_alternative<ControlSnapshot>(takeover.outcome));
    EXPECT_EQ(callbackCount, 1U);
    EXPECT_TRUE(detached);
    EXPECT_EQ(callbackFrame, published.value());
    EXPECT_EQ(
        std::get<ControlSnapshot>(takeover.outcome).mode,
        ControlMode::Local);
}

}  // namespace
}  // namespace vna::application
