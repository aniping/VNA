#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/single_sweep_command_handler.hpp>
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

template <typename Value>
Value successValue(const CommandResult& result) {
    return std::get<Value>(std::get<CommandSuccess>(result.outcome).value);
}

class TraceFrameDiscardCommandTest
    : public ::testing::Test,
      private SingleSweepExecution {
protected:
    TraceFrameDiscardCommandTest()
        : handler_(*this),
          bus_(InstrumentId{"instrument-1"}, handler_, runtimeOwner_.catalog()) {
        const auto channel = successValue<domain::ChannelId>(dispatch(
            CreateChannelCommand{.sweep = validSweep()}));
        measurementId_ = successValue<domain::MeasurementId>(dispatch(
            CreateMeasurementCommand{
                .channelId = channel,
                .type = domain::MeasurementType::S11}));
        windowId_ = successValue<display_model::WindowId>(
            dispatch(CreateWindowCommand{}));
    }

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

private:
    SingleSweepSubmitResult submit(SingleSweepWorkItem) override {
        return SingleSweepSubmitError{
            .code = SingleSweepSubmitErrorCode::Stopped};
    }

    void invalidateTraceFrame(
        display_model::TraceId traceId) noexcept override {
        repository_.discard(traceId);
    }

    void discardTrace(display_model::TraceId traceId) noexcept override {
        ++discardCalls_;
        repository_.discard(traceId);
    }

protected:
    vna::test::CommandBusRuntimeOwner runtimeOwner_{{}, 1};
    TraceDisplayFrameRepository& repository_{runtimeOwner_.repository()};
    std::size_t discardCalls_{0};
    SingleSweepCommandHandler handler_;
    CommandBus bus_;
    domain::MeasurementId measurementId_{0};
    display_model::WindowId windowId_{0};
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
    EXPECT_EQ(discardCalls_, 0U);
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
    EXPECT_EQ(discardCalls_, 0U);
    EXPECT_EQ(repository_.latest(traceId), published.value());
}

}  // namespace
}  // namespace vna::application
