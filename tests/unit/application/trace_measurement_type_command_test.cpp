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

class TraceMeasurementTypeCommandTest : public ::testing::Test {
protected:
    TraceMeasurementTypeCommandTest()
        : bus_(InstrumentId{"instrument-1"},
               vna::test::stoppedSingleSweepHandler(),
               runtimeOwner_.catalog(),
               std::move(preset_.commandBusState)) {}

    CommandResult dispatch(
        CommandPayload payload,
        std::string commandId = {}) {
        if (commandId.empty()) {
            commandId = "measurement-type-" + std::to_string(nextId_++);
        }
        return bus_.dispatch(CommandEnvelope{
            .commandId = CommandId{std::move(commandId)},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .origin = CommandOrigin::Web,
            .expectedStateRevision = bus_.snapshot().stateRevision,
            .payload = std::move(payload),
        });
    }

    TraceDisplayFrameSetHandle publishCurrent(std::uint64_t sequence) {
        const auto plan = catalog_.capture();
        const auto& target = plan->targets.front();
        auto published = catalog_.publishIfCurrent(
            plan,
            TraceDisplayFrameSet{
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
                  CartesianTraceDisplaySamples{
                      TraceDisplayUnit::Decibel, {-10.0, -11.0}}}}});
        return std::get<TraceDisplayFrameSetHandle>(std::move(published));
    }

    static const CommandSuccess& success(const CommandResult& result) {
        return std::get<CommandSuccess>(result.outcome);
    }

    FactoryPreset preset_{makeFactoryPreset()};
    vna::test::CommandBusRuntimeOwner runtimeOwner_{
        preset_.commandBusState, 4};
    TraceDisplayFrameRepository& repository_{runtimeOwner_.repository()};
    TracePublicationCatalog& catalog_{runtimeOwner_.catalog()};
    CommandBus bus_;
    std::uint64_t nextId_{1};
};

TEST_F(TraceMeasurementTypeCommandTest, ReusesMeasurementInTheSameChannel) {
    const auto existing = dispatch(CreateMeasurementCommand{
        domain::ChannelId{1}, domain::MeasurementType::S11});
    ASSERT_EQ(
        std::get<domain::MeasurementId>(success(existing).value),
        domain::MeasurementId{2});
    const auto retained = publishCurrent(1);

    const auto changed = dispatch(SetTraceMeasurementTypeCommand{
        display_model::TraceId{1}, domain::MeasurementType::S11});

    EXPECT_EQ(changed.stateRevision, 2U);
    EXPECT_EQ(
        std::get<display_model::TraceId>(success(changed).value),
        display_model::TraceId{1});
    const auto state = bus_.snapshot();
    ASSERT_EQ(state.instrument.measurements.size(), 2U);
    ASSERT_EQ(state.display.traces.size(), 1U);
    EXPECT_EQ(state.display.windows.size(), 1U);
    EXPECT_EQ(state.display.traces[0].id, display_model::TraceId{1});
    EXPECT_EQ(state.display.traces[0].windowId, display_model::WindowId{1});
    EXPECT_EQ(state.display.traces[0].measurementId, domain::MeasurementId{2});
    EXPECT_EQ(catalog_.capture()->generation, 2U);
    EXPECT_EQ(repository_.latestFrameSet(), nullptr);
    ASSERT_NE(retained, nullptr);
}

TEST_F(TraceMeasurementTypeCommandTest, CreatesEachMissingTypeAndNoOpsOnSameType) {
    const auto types = {
        domain::MeasurementType::S11,
        domain::MeasurementType::S12,
        domain::MeasurementType::S22,
    };
    std::uint64_t expectedRevision = 0;
    std::uint64_t expectedMeasurementId = 1;
    std::uint64_t expectedGeneration = 1;

    for (const auto type : types) {
        const auto changed = dispatch(SetTraceMeasurementTypeCommand{
            display_model::TraceId{1}, type});
        ++expectedRevision;
        ++expectedMeasurementId;
        ++expectedGeneration;

        EXPECT_EQ(changed.stateRevision, expectedRevision);
        EXPECT_EQ(catalog_.capture()->generation, expectedGeneration);
        const auto state = bus_.snapshot();
        EXPECT_EQ(state.instrument.measurements.size(), expectedMeasurementId);
        ASSERT_EQ(state.display.traces.size(), 1U);
        EXPECT_EQ(
            state.display.traces[0].measurementId,
            domain::MeasurementId{expectedMeasurementId});
    }

    const auto retained = publishCurrent(4);
    const auto repeated = dispatch(SetTraceMeasurementTypeCommand{
        display_model::TraceId{1}, domain::MeasurementType::S22});

    EXPECT_EQ(repeated.stateRevision, expectedRevision);
    EXPECT_EQ(catalog_.capture()->generation, expectedGeneration);
    EXPECT_EQ(repository_.latestFrameSet(), retained);
    const auto state = bus_.snapshot();
    EXPECT_EQ(state.instrument.measurements.size(), 4U);
    EXPECT_EQ(state.display.windows.size(), 1U);
    EXPECT_EQ(state.display.traces.size(), 1U);
}

}  // namespace
}  // namespace vna::application
