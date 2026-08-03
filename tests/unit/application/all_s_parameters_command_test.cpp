#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

TraceDisplayFrameSet frameSetFor(
    const TracePublicationPlanHandle& plan,
    std::uint64_t sequence) {
    std::vector<TraceDisplayFrame> frames;
    for (const auto& target : plan->targets) {
        frames.push_back({
            frames::FrameId{sequence},
            target.trace.id,
            target.measurement.id,
            target.measurement.type,
            plan->stateRevision,
            plan->generation,
            sequence,
            target.trace.format,
            {1.0, 2.0},
            CartesianTraceDisplaySamples{
                TraceDisplayUnit::Decibel, {-10.0, -11.0}},
        });
    }
    return {plan->generation, sequence, std::move(frames)};
}

void expectCanonicalDisplay(const StateSnapshot& state) {
    constexpr std::array expectedTypes{
        domain::MeasurementType::S11,
        domain::MeasurementType::S12,
        domain::MeasurementType::S21,
        domain::MeasurementType::S22,
    };
    ASSERT_EQ(state.display.windows.size(), expectedTypes.size());
    ASSERT_EQ(state.display.traces.size(), expectedTypes.size());
    for (std::size_t index = 0; index < expectedTypes.size(); ++index) {
        const auto expectedId = static_cast<std::uint64_t>(index + 1);
        const auto& trace = state.display.traces[index];
        EXPECT_EQ(trace.id, display_model::TraceId{expectedId});
        EXPECT_EQ(trace.windowId, display_model::WindowId{expectedId});
        EXPECT_EQ(trace.format, display_model::TraceFormat::LogMagnitude);
        const auto measurement = std::find_if(
            state.instrument.measurements.cbegin(),
            state.instrument.measurements.cend(),
            [&trace](const auto& item) {
                return item.id == trace.measurementId;
            });
        ASSERT_NE(measurement, state.instrument.measurements.cend());
        EXPECT_EQ(measurement->type, expectedTypes[index]);
    }
}

class AllSParametersCommandTest : public ::testing::Test {
protected:
    AllSParametersCommandTest()
        : bus_(InstrumentId{"instrument-1"},
               runtimeOwner_.runtime(),
               std::move(preset_.commandBusState)) {}

    CommandEnvelope command(
        std::string commandId = "all-s",
        display_model::TraceId traceId = display_model::TraceId{1}) const {
        return {
            .commandId = CommandId{std::move(commandId)},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .origin = CommandOrigin::Web,
            .expectedStateRevision = 0,
            .payload = EnsureAllSParametersCommand{traceId},
        };
    }

    static const CommandSuccess* success(const CommandResult& result) {
        return std::get_if<CommandSuccess>(&result.outcome);
    }

    FactoryPreset preset_{vna::test::singleSweepFactoryPreset()};
    vna::test::CommandBusRuntimeOwner runtimeOwner_{
        preset_.commandBusState, 8};
    TraceDisplayFrameRepository& repository_{runtimeOwner_.repository()};
    TracePublicationCatalog& catalog_{runtimeOwner_.catalog()};
    CommandBus bus_;
};

TEST_F(AllSParametersCommandTest, CreatesFourSingleTraceDiagramsAtomically) {
    const auto initialPlan = catalog_.capture();
    const auto retained = catalog_.publishIfCurrent(
        initialPlan, frameSetFor(initialPlan, 1));
    ASSERT_TRUE(
        std::holds_alternative<TraceDisplayFrameSetHandle>(retained));
    const auto result = bus_.dispatch(command());

    ASSERT_NE(success(result), nullptr);
    EXPECT_EQ(result.stateRevision, 1U);
    EXPECT_EQ(
        std::get<display_model::TraceId>(success(result)->value),
        display_model::TraceId{1});
    const auto state = bus_.snapshot();
    ASSERT_EQ(state.instrument.measurements.size(), 4U);
    expectCanonicalDisplay(state);
    EXPECT_EQ(catalog_.capture()->generation, 2U);
    EXPECT_EQ(catalog_.capture()->targets.size(), 4U);
    EXPECT_EQ(repository_.latestFrameSet(), nullptr);

    for (const auto& trace : state.display.traces) {
        EXPECT_EQ(
            std::count_if(
                state.display.traces.cbegin(), state.display.traces.cend(),
                [&trace](const auto& item) {
                    return item.windowId == trace.windowId;
                }),
            1);
    }
}

TEST_F(AllSParametersCommandTest, ReusesMeasurementsAndNoOpsWhenComplete) {
    const auto first = bus_.dispatch(command("first"));
    ASSERT_NE(success(first), nullptr);
    const auto plan = catalog_.capture();
    const auto retained = catalog_.publishIfCurrent(plan, frameSetFor(plan, 2));
    ASSERT_TRUE(
        std::holds_alternative<TraceDisplayFrameSetHandle>(retained));

    auto repeated = command("second");
    repeated.expectedStateRevision = 1;
    const auto second = bus_.dispatch(repeated);

    ASSERT_NE(success(second), nullptr);
    EXPECT_EQ(second.stateRevision, 1U);
    EXPECT_EQ(catalog_.capture(), plan);
    EXPECT_EQ(
        repository_.latestFrameSet(),
        std::get<TraceDisplayFrameSetHandle>(retained));
    EXPECT_EQ(bus_.snapshot().instrument.measurements.size(), 4U);
    EXPECT_EQ(bus_.snapshot().display.windows.size(), 4U);
    EXPECT_EQ(bus_.snapshot().display.traces.size(), 4U);
}

TEST_F(AllSParametersCommandTest, RejectsMissingAnchorWithoutMutation) {
    const auto rejected =
        bus_.dispatch(command("missing", display_model::TraceId{99}));

    const auto* error = std::get_if<CommandError>(&rejected.outcome);
    ASSERT_NE(error, nullptr);
    ASSERT_NE(std::get_if<display_model::DisplayError>(error), nullptr);
    EXPECT_EQ(
        std::get<display_model::DisplayError>(*error).code,
        display_model::DisplayErrorCode::TraceNotFound);
    EXPECT_EQ(rejected.stateRevision, 0U);
    EXPECT_EQ(bus_.snapshot().display.traces.size(), 1U);
    EXPECT_EQ(catalog_.capture()->generation, 1U);
}

TEST_F(AllSParametersCommandTest, ReplaysExactCommandAndRejectsIdReuse) {
    const auto original = command();
    const auto first = bus_.dispatch(original);
    const auto replay = bus_.dispatch(original);
    auto reused = original;
    reused.payload = EnsureAllSParametersCommand{display_model::TraceId{2}};
    const auto rejected = bus_.dispatch(reused);

    ASSERT_NE(success(first), nullptr);
    ASSERT_NE(success(replay), nullptr);
    EXPECT_EQ(replay.stateRevision, first.stateRevision);
    EXPECT_EQ(success(replay)->value, success(first)->value);
    const auto* error = std::get_if<CommandError>(&rejected.outcome);
    ASSERT_NE(error, nullptr);
    ASSERT_NE(std::get_if<ApplicationError>(error), nullptr);
    EXPECT_EQ(
        std::get<ApplicationError>(*error).code,
        ApplicationErrorCode::CommandIdReuse);
    EXPECT_EQ(rejected.stateRevision, 1U);
    EXPECT_EQ(bus_.snapshot().display.traces.size(), 4U);
}

}  // namespace
}  // namespace vna::application
