#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vna/compat/stop_token.hpp>
#include <string>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>
#include <vna/test/sweep_status_test_support.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

class BlockingSource {
public:
    acquisition::RawSweepCaptureResult operator()(
        const acquisition::RawSweepCaptureRequest& request,
        const acquisition::RawSweepChunkObserver&,
        vna::compat::StopToken token) {
        vna::compat::StopCallback notify{token, [this] {
            std::lock_guard lock{mutex_};
            changed_.notify_all();
        }};
        std::unique_lock lock{mutex_};
        requested_ = request.sequenceNumber;
        plan_ = request.plan;
        changed_.notify_all();
        changed_.wait(lock, [&] {
            return token.stopRequested() || released_ >= requested_;
        });
        return token.stopRequested()
            ? acquisition::RawSweepCaptureResult{
                  acquisition::RawSweepCaptureCanceled{}}
            : acquisition::RawSweepCaptureResult{
                  acquisition::test_support::validPayload(requested_)};
    }

    bool waitFor(std::uint64_t sequence) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] {
            return requested_ >= sequence;
        });
    }

    void release(std::uint64_t sequence) {
        std::lock_guard lock{mutex_};
        released_ = sequence;
        changed_.notify_all();
    }

    acquisition::ContinuousAcquisitionPlan plan() const {
        std::lock_guard lock{mutex_};
        return plan_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t requested_{};
    std::uint64_t released_{};
    acquisition::ContinuousAcquisitionPlan plan_{
        acquisition::test_support::validPlan()};
};

StateSnapshot presetSnapshot(const FactoryPreset& preset) {
    return {0, {}, preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot()};
}

class CommandBusConfigurationTest : public ::testing::Test {
protected:
    CommandBusConfigurationTest()
        : catalog_(preset_.acquisitionChannelId, repository_,
                   presetSnapshot(preset_)),
          runtime_({preset_.acquisitionPlan, catalog_.capture(), 32},
                   std::ref(source_), previews_, catalog_, operations_),
          bus_(InstrumentId{"instrument-1"},
               runtime_,
               std::move(preset_.commandBusState)) {}

    CommandResult dispatch(CommandPayload payload) {
        return bus_.dispatch(CommandEnvelope{
            CommandId{"config-" + std::to_string(++commandId_)},
            SessionId{"session-1"}, InstrumentId{"instrument-1"},
            CommandOrigin::Web, bus_.snapshot().stateRevision,
            std::move(payload)});
    }

    static void expectSuccess(const CommandResult& result) {
        EXPECT_NE(std::get_if<CommandSuccess>(&result.outcome), nullptr);
    }

    FactoryPreset preset_{makeFactoryPreset()};
    BlockingSource source_;
    TraceDisplayFrameRepository repository_{8};
    SweepPreviewExchange previews_{
        vna::test::testSweepStatus(preset_.acquisitionPlan)};
    OperationManager operations_;
    TracePublicationCatalog catalog_;
    SweepRuntime runtime_;
    CommandBus bus_;
    std::uint64_t commandId_{};
};

TEST_F(CommandBusConfigurationTest,
       SweepMutationIsVisibleBeforeBoundaryThenAppliedAtomically) {
    ASSERT_TRUE(source_.waitFor(1));
    auto sweep = bus_.snapshot().instrument.channels[0].sweep;
    sweep.startFrequencyHz = 20'000'000;

    const auto changed = dispatch(UpdateChannelSweepCommand{
        domain::ChannelId{1}, sweep});

    expectSuccess(changed);
    EXPECT_EQ(changed.stateRevision, 1U);
    EXPECT_EQ(bus_.snapshot().instrument.channels[0].sweep.startFrequencyHz,
              20'000'000U);
    EXPECT_EQ(runtime_.snapshot().appliedStateRevision, 0U);
    EXPECT_EQ(runtime_.snapshot().configuredStateRevision, 1U);
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    source_.release(1);
    ASSERT_TRUE(source_.waitFor(2));
    EXPECT_EQ(runtime_.snapshot().appliedStateRevision, 1U);
    EXPECT_EQ(runtime_.snapshot().appliedGeneration, 2U);
    EXPECT_EQ(source_.plan().frequencyAxis.startFrequencyHz, 20'000'000U);
    runtime_.stop();
}

TEST_F(CommandBusConfigurationTest, IdenticalSweepSettingsAreANoOp) {
    const auto before = bus_.snapshot();
    const auto result = dispatch(UpdateChannelSweepCommand{
        domain::ChannelId{1}, before.instrument.channels[0].sweep});

    expectSuccess(result);
    EXPECT_EQ(result.stateRevision, 0U);
    EXPECT_EQ(runtime_.snapshot().configuredStateRevision, 0U);
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    runtime_.stop();
}

TEST_F(CommandBusConfigurationTest,
       LatestFullCandidateReplacesEarlierPendingConfiguration) {
    ASSERT_TRUE(source_.waitFor(1));
    auto sweep = bus_.snapshot().instrument.channels[0].sweep;
    sweep.startFrequencyHz = 21'000'000;
    expectSuccess(dispatch(UpdateChannelSweepCommand{
        domain::ChannelId{1}, sweep}));
    expectSuccess(dispatch(UpdateTraceFormatCommand{
        display_model::TraceId{1}, display_model::TraceFormat::Phase}));

    source_.release(1);
    ASSERT_TRUE(source_.waitFor(2));
    const auto applied = catalog_.capture();
    EXPECT_EQ(applied->generation, 2U);
    EXPECT_EQ(applied->stateRevision, 2U);
    EXPECT_EQ(applied->targets[0].trace.format,
              display_model::TraceFormat::Phase);
    EXPECT_EQ(source_.plan().frequencyAxis.startFrequencyHz, 21'000'000U);
    runtime_.stop();
}

TEST_F(CommandBusConfigurationTest,
       RuntimeRejectionLeavesStateRevisionAndCacheUntouched) {
    const auto catalogBefore = catalog_.capture();
    runtime_.stop();
    const auto before = bus_.snapshot();
    auto sweep = before.instrument.channels[0].sweep;
    ++sweep.startFrequencyHz;

    const auto rejected = dispatch(UpdateChannelSweepCommand{
        domain::ChannelId{1}, sweep});

    const auto* error = std::get_if<CommandError>(&rejected.outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(commandErrorCode(*error), CommandErrorCode::ResourceBusy);
    EXPECT_EQ(rejected.stateRevision, 0U);
    EXPECT_EQ(bus_.snapshot().instrument.channels[0].sweep.startFrequencyHz,
              before.instrument.channels[0].sweep.startFrequencyHz);
    EXPECT_EQ(runtime_.snapshot().configuredStateRevision, 0U);
    EXPECT_EQ(catalog_.capture(), catalogBefore);
    EXPECT_EQ(repository_.latestFrameSet(), nullptr);
    EXPECT_EQ(bus_.stats().idempotencyEntries, 0U);
}

TEST_F(CommandBusConfigurationTest,
       ScaleRevisionAppliesWithoutAdvancingGeneration) {
    ASSERT_TRUE(source_.waitFor(1));
    const auto changed = dispatch(UpdateTraceScalePerDivisionCommand{
        display_model::TraceId{1}, 5.0});
    expectSuccess(changed);
    source_.release(1);
    ASSERT_TRUE(source_.waitFor(2));

    EXPECT_EQ(runtime_.snapshot().appliedStateRevision, 1U);
    EXPECT_EQ(runtime_.snapshot().appliedGeneration, 1U);
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    runtime_.stop();
}

}  // namespace
}  // namespace vna::application
