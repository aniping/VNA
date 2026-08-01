#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <variant>

#include <vna/application/factory_preset.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

class TraceDisplayFrameQueryIdentityTest : public ::testing::Test {
protected:
    TraceDisplayFrameQueryIdentityTest()
        : traceId_(preset_.continuousTracePreset.trace.id),
          measurementId_(preset_.continuousTracePreset.measurement.id),
          measurementType_(preset_.continuousTracePreset.measurement.type),
          bus_(
              InstrumentId{"instrument-1"},
              std::move(preset_.commandBusState)),
          query_(bus_, repository_) {}

    TraceDisplayFrame frame(
        display_model::TraceFormat format,
        std::uint64_t sequence) const {
        TraceDisplaySamples samples = CartesianTraceDisplaySamples{
            .unit = format == display_model::TraceFormat::Phase
                ? TraceDisplayUnit::Degree
                : TraceDisplayUnit::Decibel,
            .values = {-6.0, 45.0}};
        if (format == display_model::TraceFormat::Smith) {
            samples = ComplexTraceDisplaySamples{
                .unit = TraceDisplayUnit::Unitless,
                .values = {{0.5, -0.25}, {1.25, 0.75}}};
        }
        return {
            .frameId = frames::FrameId{sequence},
            .traceId = traceId_,
            .measurementId = measurementId_,
            .measurementType = measurementType_,
            .stateRevision = bus_.snapshot().stateRevision,
            .generation = 1,
            .sequenceNumber = sequence,
            .format = format,
            .frequenciesHz = {1.0e6, 2.0e6},
            .samples = std::move(samples),
        };
    }

    void updateFormat(display_model::TraceFormat format) {
        const auto result = bus_.dispatch(CommandEnvelope{
            .commandId = CommandId{"format-" + std::to_string(nextCommand_++)},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .origin = CommandOrigin::Web,
            .expectedStateRevision = std::nullopt,
            .payload = UpdateTraceFormatCommand{traceId_, format},
        });
        ASSERT_TRUE(std::holds_alternative<CommandSuccess>(result.outcome));
    }

    void expectUnavailable() const {
        const auto outcome = query_.latest(traceId_);
        const auto* error = std::get_if<TraceDisplayFrameQueryError>(&outcome);
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(
            error->code,
            TraceDisplayFrameQueryErrorCode::FrameNotAvailable);
    }

    FactoryPreset preset_{makeFactoryPreset()};
    const display_model::TraceId traceId_;
    const domain::MeasurementId measurementId_;
    const domain::MeasurementType measurementType_;
    vna::test::StoppedCommandBus bus_;
    TraceDisplayFrameRepository repository_{1};
    TraceDisplayFrameQuery query_;
    std::uint64_t nextCommand_{1};
};

TEST_F(
    TraceDisplayFrameQueryIdentityTest,
    RejectsFramesFromAnotherMeasurementIdentityOrType) {
    auto wrongIdentity = frame(display_model::TraceFormat::LogMagnitude, 1);
    wrongIdentity.measurementId = domain::MeasurementId{99};
    ASSERT_TRUE(repository_.publish(std::move(wrongIdentity)).hasValue());
    expectUnavailable();
    repository_.discard(traceId_);
    auto wrongType = frame(display_model::TraceFormat::LogMagnitude, 2);
    wrongType.measurementType = domain::MeasurementType::S11;
    ASSERT_TRUE(repository_.publish(std::move(wrongType)).hasValue());

    expectUnavailable();
}

TEST_F(
    TraceDisplayFrameQueryIdentityTest,
    ReturnsPhaseAndSmithWhenCurrentBindingMatches) {
    updateFormat(display_model::TraceFormat::Phase);
    ASSERT_TRUE(repository_.publish(frame(display_model::TraceFormat::Phase, 1))
                    .hasValue());
    const auto phase = query_.latest(traceId_);
    const auto* phaseFrame = std::get_if<TraceDisplayFrameHandle>(&phase);
    ASSERT_NE(phaseFrame, nullptr);
    EXPECT_TRUE(std::holds_alternative<CartesianTraceDisplaySamples>(
        (*phaseFrame)->samples));

    updateFormat(display_model::TraceFormat::Smith);
    ASSERT_TRUE(repository_.publish(frame(display_model::TraceFormat::Smith, 2))
                    .hasValue());
    const auto smith = query_.latest(traceId_);
    const auto* smithFrame = std::get_if<TraceDisplayFrameHandle>(&smith);
    ASSERT_NE(smithFrame, nullptr);
    EXPECT_TRUE(std::holds_alternative<ComplexTraceDisplaySamples>(
        (*smithFrame)->samples));
}

}  // namespace
}  // namespace vna::application
