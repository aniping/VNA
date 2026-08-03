#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <utility>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

class BlockingRawSource {
public:
    acquisition::RawSweepCaptureResult operator()(
        const acquisition::RawSweepCaptureRequest&,
        const acquisition::RawSweepChunkObserver&,
        std::stop_token token) {
        std::stop_callback notify{token, [this] {
            std::lock_guard lock{mutex_};
            changed_.notify_all();
        }};
        std::unique_lock lock{mutex_};
        started_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return token.stop_requested(); });
        return acquisition::RawSweepCaptureCanceled{};
    }

    bool waitForStart() {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] { return started_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool started_{};
};

StateSnapshot presetSnapshot(const FactoryPreset& preset) {
    return {0, {}, preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot()};
}

CommandEnvelope controlCommand(
    std::uint64_t revision,
    domain::SweepMode mode,
    std::uint32_t sweepCount) {
    return {
        CommandId{"sweep-control"}, SessionId{"session-1"},
        InstrumentId{"instrument-1"}, CommandOrigin::Web, revision,
        UpdateChannelSweepControlCommand{
            domain::ChannelId{1}, mode, sweepCount},
    };
}

TEST(SweepRuntimeModeTest, CommandStagesSweepCountAndSameValueNoOps) {
    auto preset = makeFactoryPreset();
    ASSERT_TRUE(preset.commandBusState.instrument.updateChannelSweepControl(
        domain::ChannelId{1}, domain::SweepMode::Single, 1).hasValue());
    vna::test::CommandBusRuntimeOwner owner{preset.commandBusState};
    CommandBus bus{InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler(), owner.runtime(),
        std::move(preset.commandBusState)};

    const auto changed = bus.dispatch(controlCommand(
        0, domain::SweepMode::Single, 3));
    ASSERT_NE(std::get_if<CommandSuccess>(&changed.outcome), nullptr);
    EXPECT_EQ(changed.stateRevision, 1U);
    EXPECT_EQ(bus.snapshot().instrument.channels[0].sweepCount, 3U);
    EXPECT_EQ(owner.runtime().snapshot().configuredExecution.sweepCount, 3U);
    EXPECT_EQ(owner.runtime().snapshot().appliedExecution.sweepCount, 3U);
    EXPECT_EQ(owner.runtime().snapshot().appliedGeneration, 1U);

    const auto replayed = bus.dispatch(controlCommand(
        0, domain::SweepMode::Single, 3));
    EXPECT_EQ(replayed.stateRevision, 1U);
    EXPECT_EQ(bus.snapshot().stateRevision, 1U);

    const auto reused = bus.dispatch(controlCommand(
        0, domain::SweepMode::Single, 4));
    EXPECT_EQ(reused.stateRevision, 1U);
    ASSERT_NE(std::get_if<CommandError>(&reused.outcome), nullptr);
    EXPECT_EQ(commandErrorCode(std::get<CommandError>(reused.outcome)),
              CommandErrorCode::CommandIdReuse);
    EXPECT_EQ(bus.snapshot().instrument.channels[0].sweepCount, 3U);

    auto replay = controlCommand(1, domain::SweepMode::Single, 3);
    replay.commandId = CommandId{"sweep-control-no-op"};
    const auto noOp = bus.dispatch(replay);
    EXPECT_EQ(noOp.stateRevision, 1U);
}

TEST(SweepRuntimeModeTest, InvalidSweepControlHasNoVisibleSideEffect) {
    auto preset = makeFactoryPreset();
    vna::test::CommandBusRuntimeOwner owner{preset.commandBusState};
    CommandBus bus{InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler(), owner.runtime(),
        std::move(preset.commandBusState)};

    for (const auto count : {0U, 100'001U}) {
        auto command = controlCommand(
            0, domain::SweepMode::Single, count);
        command.commandId = CommandId{
            count == 0 ? "zero-count" : "excessive-count"};
        const auto rejected = bus.dispatch(command);
        ASSERT_NE(std::get_if<CommandError>(&rejected.outcome), nullptr);
        EXPECT_EQ(commandErrorCode(std::get<CommandError>(rejected.outcome)),
                  CommandErrorCode::InvalidSweepSettings);
    }
    auto invalidMode = controlCommand(
        0, static_cast<domain::SweepMode>(99), 1);
    invalidMode.commandId = CommandId{"invalid-mode"};
    const auto rejectedMode = bus.dispatch(invalidMode);
    ASSERT_NE(std::get_if<CommandError>(&rejectedMode.outcome), nullptr);
    EXPECT_EQ(commandErrorCode(std::get<CommandError>(rejectedMode.outcome)),
              CommandErrorCode::InvalidSweepSettings);
    EXPECT_EQ(bus.snapshot().stateRevision, 0U);
    EXPECT_EQ(bus.snapshot().instrument.channels[0].sweepCount, 1U);
    EXPECT_EQ(owner.runtime().snapshot().configuredStateRevision, 0U);
}

TEST(SweepRuntimeModeTest, ContinuousConfigurationWakesHeldWorker) {
    auto preset = makeFactoryPreset();
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{
        preset.acquisitionChannelId, repository, presetSnapshot(preset)};
    SweepPreviewExchange previews;
    OperationManager operations;
    BlockingRawSource source;
    SweepRuntime runtime{
        {preset.acquisitionPlan, catalog.capture(), 32,
         {domain::SweepMode::Single, 1}},
        std::ref(source), previews, catalog, operations};

    auto prepared = runtime.prepareConfiguration(presetSnapshot(preset));
    ASSERT_TRUE(std::holds_alternative<
                PreparedSweepRuntimeConfiguration>(prepared));
    runtime.commitConfiguration(std::get<PreparedSweepRuntimeConfiguration>(
        std::move(prepared)));

    EXPECT_TRUE(source.waitForStart());
    EXPECT_EQ(runtime.snapshot().appliedExecution.mode,
              domain::SweepMode::Continuous);
    runtime.stop();
}

}  // namespace
}  // namespace vna::application
