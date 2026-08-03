#include <gtest/gtest.h>

#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
StateSnapshot runtimeState(const FactoryPreset& preset) {
    auto state = StateSnapshot{
        0, {}, preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot()};
    const auto acquisition = acquisition::test_support::validPlan();
    auto& sweep = state.instrument.channels.front().sweep;
    sweep.startFrequencyHz = acquisition.frequencyAxis.startFrequencyHz;
    sweep.stopFrequencyHz = acquisition.frequencyAxis.stopFrequencyHz;
    sweep.points = acquisition.frequencyAxis.points;
    sweep.ifBandwidthHz = acquisition.ifBandwidthHz;
    sweep.powerDbm = acquisition.powerDbm;
    state.instrument.channels.front().sweepMode = domain::SweepMode::Single;
    state.instrument.channels.front().sweepCount = 1;
    return state;
}
acquisition::RawSweepCaptureResult completeSweep(
    const acquisition::RawSweepCaptureRequest& request,
    const acquisition::RawSweepChunkObserver&,
    std::stop_token) {
    return acquisition::test_support::validPayload(request.sequenceNumber);
}
TEST(SweepRuntimeGenerationStatusTest,
     MaterialGenerationMarksOnlyItsFirstCompleteSweep) {
    auto preset = makeFactoryPreset();
    auto state = runtimeState(preset);
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{
        preset.acquisitionChannelId, repository, state};
    OperationManager operations;
    auto plan = SweepRuntimePlan{
        acquisition::test_support::validPlan(), catalog.capture(), 2,
        {domain::SweepMode::Single, 1}};
    SweepPreviewExchange previews{initialSweepRuntimeStatus(plan)};
    SweepRuntime runtime{
        std::move(plan), completeSweep, previews, catalog, operations};
    auto candidate = state;
    candidate.stateRevision = 1;
    candidate.instrument.channels.front().sweep.startFrequencyHz = 1'200'000;
    auto prepared = runtime.prepareConfiguration(candidate);
    ASSERT_TRUE(std::holds_alternative<
                PreparedSweepRuntimeConfiguration>(prepared));

    runtime.commitConfiguration(std::get<PreparedSweepRuntimeConfiguration>(
        std::move(prepared)));
    auto applied = runtime.snapshot();
    EXPECT_EQ(applied.appliedGeneration, 2U);
    EXPECT_TRUE(applied.firstSweepAfterConfiguration);
    EXPECT_EQ(applied.phase, SweepUserPhase::Hold);
    EXPECT_EQ(applied.progress, (SweepAcquisitionProgress{6, 6}));

    ASSERT_TRUE(std::holds_alternative<OperationId>(runtime.requestRestart(
        domain::ChannelId{1}, {
            CommandId{"first-generation-sweep"}, SessionId{"session"}, 1})));
    ASSERT_TRUE(repository.waitForNextSet({2, 0}).has_value());
    runtime.stop();
    const auto completed = runtime.snapshot();
    EXPECT_FALSE(completed.firstSweepAfterConfiguration);
    EXPECT_EQ(completed.phase, SweepUserPhase::Hold);
    EXPECT_EQ(completed.progress, (SweepAcquisitionProgress{6, 6}));
}

}  // namespace
}  // namespace vna::application
