#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

TraceDisplaySamples samplesFor(display_model::TraceFormat format) {
    if (format == display_model::TraceFormat::Smith) {
        return ComplexTraceDisplaySamples{
            TraceDisplayUnit::Unitless, {{0.1, 0.2}, {0.2, 0.3}}};
    }
    const auto unit = format == display_model::TraceFormat::Phase
        ? TraceDisplayUnit::Degree
        : TraceDisplayUnit::Decibel;
    return CartesianTraceDisplaySamples{unit, {-10.0, -11.0}};
}

class TraceConfigurationTransactionTest : public ::testing::Test {
protected:
    TraceConfigurationTransactionTest()
        : bus_(InstrumentId{"instrument-1"},
               vna::test::stoppedSingleSweepHandler(),
               runtimeOwner_.runtime(),
               std::move(preset_.commandBusState)) {}

    CommandResult dispatch(CommandPayload payload) {
        return bus_.dispatch(CommandEnvelope{
            .commandId = CommandId{"trace-config-" + std::to_string(nextId_++)},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .origin = CommandOrigin::Web,
            .expectedStateRevision = bus_.snapshot().stateRevision,
            .payload = std::move(payload),
        });
    }

    TraceDisplayFrameSet frameSetFor(
        const TracePublicationPlanHandle& plan,
        std::uint64_t sequence) {
        const auto& target = plan->targets.front();
        return {
            plan->generation,
            sequence,
            {{frames::FrameId{sequence},
              target.trace.id,
              target.measurement.id,
              target.measurement.type,
              plan->stateRevision,
              plan->generation,
              sequence,
              target.trace.format,
              {1.0, 2.0},
              samplesFor(target.trace.format)}},
        };
    }

    TraceDisplayFrameSetHandle publishCurrent(std::uint64_t sequence) {
        const auto plan = catalog_.capture();
        auto published = catalog_.publishIfCurrent(
            plan, frameSetFor(plan, sequence));
        return std::get<TraceDisplayFrameSetHandle>(std::move(published));
    }

    static void expectSuccess(const CommandResult& result) {
        ASSERT_TRUE(
            std::holds_alternative<CommandSuccess>(result.outcome));
    }

    FactoryPreset preset_{vna::test::singleSweepFactoryPreset()};
    vna::test::CommandBusRuntimeOwner runtimeOwner_{
        preset_.commandBusState, 4};
    TraceDisplayFrameRepository& repository_{runtimeOwner_.repository()};
    TracePublicationCatalog& catalog_{runtimeOwner_.catalog()};
    CommandBus bus_;
    std::uint64_t nextId_{1};
};

TEST_F(TraceConfigurationTransactionTest, FormatNoOpAndChangeAreAtomic) {
    const auto initialPlan = catalog_.capture();
    const auto retained = publishCurrent(1);

    const auto noOp = dispatch(UpdateTraceFormatCommand{
        display_model::TraceId{1}, display_model::TraceFormat::LogMagnitude});

    expectSuccess(noOp);
    EXPECT_EQ(noOp.stateRevision, 0U);
    EXPECT_EQ(catalog_.capture(), initialPlan);
    EXPECT_EQ(repository_.latestFrameSet(), retained);

    const auto changed = dispatch(UpdateTraceFormatCommand{
        display_model::TraceId{1}, display_model::TraceFormat::Phase});

    expectSuccess(changed);
    EXPECT_EQ(changed.stateRevision, 1U);
    EXPECT_EQ(catalog_.capture()->generation, 2U);
    EXPECT_EQ(repository_.latestFrameSet(), nullptr);
}

TEST_F(TraceConfigurationTransactionTest, ScaleKeepsGenerationAndOldPlanValid) {
    const auto initialPlan = catalog_.capture();
    const auto retained = publishCurrent(1);

    const auto changed = dispatch(UpdateTraceScalePerDivisionCommand{
        display_model::TraceId{1}, 5.0});

    expectSuccess(changed);
    EXPECT_EQ(changed.stateRevision, 1U);
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    EXPECT_NE(catalog_.capture(), initialPlan);
    EXPECT_EQ(repository_.latestFrameSet(), retained);
    const auto published = catalog_.publishIfCurrent(
        initialPlan, frameSetFor(initialPlan, 2));
    EXPECT_TRUE(
        std::holds_alternative<TraceDisplayFrameSetHandle>(published));
}

TEST_F(TraceConfigurationTransactionTest, CreateAndRemoveChangeTargetSet) {
    const auto retained = publishCurrent(1);
    const auto measurement = dispatch(CreateMeasurementCommand{
        domain::ChannelId{1}, domain::MeasurementType::S11});
    const auto window = dispatch(CreateWindowCommand{});

    expectSuccess(measurement);
    expectSuccess(window);
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    EXPECT_EQ(repository_.latestFrameSet(), retained);

    const auto created = dispatch(CreateTraceCommand{
        display_model::WindowId{2},
        domain::MeasurementId{2},
        display_model::TraceFormat::Phase});
    expectSuccess(created);
    const auto beforeRemove = catalog_.capture();
    ASSERT_EQ(beforeRemove->targets.size(), 2U);
    EXPECT_EQ(beforeRemove->generation, 2U);
    EXPECT_EQ(beforeRemove->targets[0].trace.id, display_model::TraceId{1});
    EXPECT_EQ(beforeRemove->targets[1].trace.id, display_model::TraceId{2});
    EXPECT_EQ(repository_.latestFrameSet(), nullptr);

    const auto removed = dispatch(RemoveTraceCommand{display_model::TraceId{1}});
    expectSuccess(removed);
    const auto current = catalog_.capture();
    ASSERT_EQ(current->targets.size(), 1U);
    EXPECT_EQ(current->generation, 3U);
    EXPECT_EQ(current->targets[0].trace.id, display_model::TraceId{2});
    const auto stale = catalog_.publishIfCurrent(
        beforeRemove,
        TraceDisplayFrameSet{beforeRemove->generation, 2, {}});
    ASSERT_TRUE(std::holds_alternative<TracePublicationCatalogError>(stale));
    EXPECT_EQ(
        std::get<TracePublicationCatalogError>(stale).code,
        TracePublicationCatalogErrorCode::StalePublication);
}

TEST_F(TraceConfigurationTransactionTest, CandidateFailureIsAtomic) {
    const auto initialPlan = catalog_.capture();
    const auto retained = publishCurrent(1);

    const auto rejected = dispatch(UpdateTraceFormatCommand{
        display_model::TraceId{1},
        static_cast<display_model::TraceFormat>(99)});

    const auto* commandError = std::get_if<CommandError>(&rejected.outcome);
    ASSERT_NE(commandError, nullptr);
    const auto* error = std::get_if<ApplicationError>(commandError);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ApplicationErrorCode::TraceConfigurationRejected);
    EXPECT_EQ(rejected.stateRevision, 0U);
    EXPECT_EQ(catalog_.capture(), initialPlan);
    EXPECT_EQ(repository_.latestFrameSet(), retained);
    EXPECT_EQ(
        bus_.snapshot().display.traces[0].format,
        display_model::TraceFormat::LogMagnitude);
}

}  // namespace
}  // namespace vna::application
