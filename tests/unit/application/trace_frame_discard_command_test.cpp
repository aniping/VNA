#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

constexpr domain::SweepSettings validSweep() {
    return {
        .startFrequencyHz = 1'000'000,
        .stopFrequencyHz = 2'000'000,
        .points = 5,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
    };
}

CommandBusInitialState singleChannelState() {
    CommandBusInitialState state;
    const auto channel = state.instrument.createChannel(validSweep()).value();
    static_cast<void>(state.instrument.updateChannelSweepControl(
        channel, domain::SweepMode::Single, 1));
    static_cast<void>(state.instrument.createMeasurement(
        channel, domain::MeasurementType::S11));
    static_cast<void>(state.displayWorkspace.createWindow());
    return state;
}

template <typename Value>
Value successValue(const CommandResult& result) {
    return std::get<Value>(std::get<CommandSuccess>(result.outcome).value);
}

class TraceFrameDiscardCommandTest : public ::testing::Test {
protected:
    TraceFrameDiscardCommandTest()
        : bus_(InstrumentId{"instrument-1"}, runtimeOwner_.runtime(),
               std::move(initialState_)) {}

    CommandResult dispatch(CommandPayload payload) {
        return bus_.dispatch(CommandEnvelope{
            .commandId = CommandId{
                "discard-command-" + std::to_string(nextCommandId_++)},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .origin = CommandOrigin::Web,
            .expectedStateRevision = std::nullopt,
            .payload = std::move(payload),
        });
    }

    display_model::TraceId createTrace() {
        return successValue<display_model::TraceId>(dispatch(
            CreateTraceCommand{
                .windowId = windowId_,
                .measurementId = measurementId_,
                .format = display_model::TraceFormat::LogMagnitude}));
    }

    TraceDisplayFrame frameFor(
        display_model::TraceId traceId,
        std::uint64_t frameId) {
        return TraceDisplayFrame{
            .frameId = frames::FrameId{frameId},
            .traceId = traceId,
            .measurementId = measurementId_,
            .measurementType = domain::MeasurementType::S11,
            .stateRevision = bus_.snapshot().stateRevision,
            .generation = 1,
            .sequenceNumber = 1,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = {1'000'000.0, 2'000'000.0},
            .samples = CartesianTraceDisplaySamples{
                .unit = TraceDisplayUnit::Decibel,
                .values = {-6.0, -3.0}},
        };
    }

protected:
    CommandBusInitialState initialState_{singleChannelState()};
    vna::test::CommandBusRuntimeOwner runtimeOwner_{initialState_, 1};
    TraceDisplayFrameRepository& repository_{runtimeOwner_.repository()};
    CommandBus bus_;
    domain::MeasurementId measurementId_{1};
    display_model::WindowId windowId_{1};
    std::uint64_t nextCommandId_{1};
};

TEST_F(TraceFrameDiscardCommandTest, CatalogRemovalReleasesTraceCapacity) {
    TraceDisplayFrameHandle firstReader;
    for (std::uint64_t cycle = 1; cycle <= 3; ++cycle) {
        const auto traceId = createTrace();
        const auto published = repository_.publish(frameFor(traceId, cycle));
        ASSERT_TRUE(published.hasValue());
        if (!firstReader) {
            firstReader = published.value();
        }

        const auto removed = dispatch(RemoveTraceCommand{.traceId = traceId});

        EXPECT_TRUE(std::holds_alternative<CommandSuccess>(removed.outcome));
        EXPECT_EQ(repository_.latest(traceId), nullptr);
    }
    ASSERT_NE(firstReader, nullptr);
    EXPECT_EQ(firstReader->traceId, display_model::TraceId{1});
}

TEST_F(TraceFrameDiscardCommandTest, FailedRemovalDoesNotDiscardFrame) {
    const auto traceId = createTrace();
    const auto published = repository_.publish(frameFor(traceId, 1));
    ASSERT_TRUE(published.hasValue());
    const auto revision = bus_.snapshot().stateRevision;

    const auto result = dispatch(
        RemoveTraceCommand{.traceId = display_model::TraceId{999}});

    const auto* error = std::get_if<CommandError>(&result.outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(commandErrorCode(*error), CommandErrorCode::TraceNotFound);
    EXPECT_EQ(result.stateRevision, revision);
    EXPECT_EQ(repository_.latest(traceId), published.value());
}

}  // namespace
}  // namespace vna::application
