#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>
#include <vna/test/sweep_status_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

class BoundarySource {
public:
    acquisition::RawSweepCaptureResult operator()(
        const acquisition::RawSweepCaptureRequest& request,
        const acquisition::RawSweepChunkObserver&,
        std::stop_token token) {
        std::stop_callback notify{token, [this] {
            std::lock_guard lock{mutex_};
            changed_.notify_all();
        }};
        std::unique_lock lock{mutex_};
        requested_ = request.sequenceNumber;
        changed_.notify_all();
        changed_.wait(lock, [&] {
            return token.stop_requested() || released_ >= requested_;
        });
        return token.stop_requested()
            ? acquisition::RawSweepCaptureResult{
                  acquisition::RawSweepCaptureCanceled{}}
            : acquisition::RawSweepCaptureResult{
                  acquisition::test_support::validPayload(requested_)};
    }

    bool waitFor(std::uint64_t sequence) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] { return requested_ >= sequence; });
    }

    void release(std::uint64_t sequence) {
        std::lock_guard lock{mutex_};
        released_ = sequence;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t requested_{};
    std::uint64_t released_{};
};

FactoryPreset alignedPreset() {
    auto preset = makeFactoryPreset();
    const auto plan = acquisition::test_support::validPlan();
    const auto updated = preset.commandBusState.instrument.updateChannelSweep(
        preset.acquisitionChannelId,
        {plan.frequencyAxis.startFrequencyHz,
         plan.frequencyAxis.stopFrequencyHz,
         plan.frequencyAxis.points,
         plan.ifBandwidthHz,
         plan.powerDbm});
    if (!updated.hasValue()) {
        throw std::logic_error{"invalid runtime state test preset"};
    }
    return preset;
}

StateSnapshot initialSnapshot(const FactoryPreset& preset) {
    return {
        0,
        {},
        preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot(),
    };
}

class RuntimeStateOwner {
public:
    RuntimeStateOwner()
        : preset_(alignedPreset()),
          initial_(initialSnapshot(preset_)),
          catalog_(preset_.acquisitionChannelId, repository_, initial_),
          runtime_({acquisition::test_support::validPlan(), catalog_.capture(), 2},
                   std::ref(source_), previews_, catalog_, operations_),
          bus_(InstrumentId{"instrument-1"}, runtime_,
               std::move(preset_.commandBusState)) {}

    BoundarySource source_;
    FactoryPreset preset_;
    StateSnapshot initial_;
    TraceDisplayFrameRepository repository_{4};
    TracePublicationCatalog catalog_;
    SweepPreviewExchange previews_{vna::test::testSweepStatus()};
    OperationManager operations_;
    SweepRuntime runtime_;
    CommandBus bus_;
};

TEST(CommandBusRuntimeStateTest,
     SnapshotKeepsConfiguredAndAppliedStatesAtOneSweepBoundary) {
    RuntimeStateOwner owner;
    ASSERT_TRUE(owner.source_.waitFor(1));
    auto sweep = owner.bus_.snapshot().instrument.channels.front().sweep;
    sweep.startFrequencyHz = 1'200'000;

    const auto result = owner.bus_.dispatch({
        CommandId{"update-sweep"}, SessionId{"session-1"},
        InstrumentId{"instrument-1"}, CommandOrigin::Web, 0,
        UpdateChannelSweepCommand{domain::ChannelId{1}, sweep}});

    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(result.outcome));
    const auto configured = owner.bus_.snapshot();
    EXPECT_EQ(configured.stateRevision, 1U);
    EXPECT_EQ(configured.instrument.channels.front().sweep.startFrequencyHz,
              1'200'000U);
    EXPECT_EQ(configured.sweepRuntime.configuredStateRevision, 1U);
    EXPECT_EQ(configured.sweepRuntime.appliedStateRevision, 0U);
    EXPECT_EQ(configured.sweepRuntime.appliedGeneration, 1U);

    owner.source_.release(1);
    ASSERT_TRUE(owner.source_.waitFor(2));
    const auto applied = owner.bus_.snapshot();
    EXPECT_EQ(applied.sweepRuntime.configuredStateRevision, 1U);
    EXPECT_EQ(applied.sweepRuntime.appliedStateRevision, 1U);
    EXPECT_EQ(applied.sweepRuntime.appliedGeneration, 2U);
    owner.runtime_.stop();
}

}  // namespace
}  // namespace vna::application
