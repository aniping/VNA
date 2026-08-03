#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/test/sweep_status_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

class DelayedCancelSource {
public:
    acquisition::RawSweepCaptureResult operator()(
        const acquisition::RawSweepCaptureRequest&,
        const acquisition::RawSweepChunkObserver&,
        std::stop_token token) {
        std::stop_callback notify{token, [this] {
            std::lock_guard lock{mutex_};
            cancellationSeen_ = true;
            changed_.notify_all();
        }};
        std::unique_lock lock{mutex_};
        started_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] {
            return token.stop_requested() && released_;
        });
        return acquisition::RawSweepCaptureCanceled{};
    }

    bool waitForStart() {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] { return started_; });
    }

    bool waitForCancellation() {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] { return cancellationSeen_; });
    }

    void release() {
        std::lock_guard lock{mutex_};
        released_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool started_{};
    bool cancellationSeen_{};
    bool released_{};
};

StateSnapshot presetState(const FactoryPreset& preset) {
    return {0, {}, preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot()};
}

CommandEnvelope restartCommand(const char* id, const char* session) {
    return {
        CommandId{id}, SessionId{session}, InstrumentId{"instrument-1"},
        CommandOrigin::Web, 0,
        StartSingleSweepCommand{domain::ChannelId{1}},
    };
}

TEST(StartSingleSweepReentrancyTest, ReplacedQueuedOperationCallbackReentersBusAfterUnlock) {
    auto preset = makeFactoryPreset();
    static_cast<void>(preset.commandBusState.instrument.updateChannelSweepControl(
        domain::ChannelId{1}, domain::SweepMode::Single, 1));
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{
        preset.acquisitionChannelId, repository, presetState(preset)};
    SweepPreviewExchange previews{
        vna::test::testSweepStatus(preset.acquisitionPlan)};
    OperationManager operations;
    DelayedCancelSource source;
    SweepRuntime runtime{{preset.acquisitionPlan, catalog.capture(), 32,
                          {domain::SweepMode::Single, 1}},
                         std::ref(source), previews, catalog, operations};
    CommandBus bus{InstrumentId{"instrument-1"}, runtime, std::move(preset.commandBusState)};
    const auto first = bus.dispatch(restartCommand("first", "session-1"));
    const auto started = source.waitForStart();
    const auto second = bus.dispatch(restartCommand("second", "session-2"));
    const auto cancellationSeen = source.waitForCancellation();
    auto fence = operations.captureFence(SessionId{"session-2"});
    std::promise<void> attempting;
    auto attemptingFuture = attempting.get_future();
    std::promise<void> reentered;
    auto reenteredFuture = reentered.get_future();
    std::thread reentryThread;
    bool callbackSawUnlocked{};
    auto subscription = operations.subscribe(std::move(fence), [&] {
        reentryThread = std::thread{[&] {
            attempting.set_value();
            static_cast<void>(bus.snapshot());
            reentered.set_value();
        }};
        attemptingFuture.wait();
        callbackSawUnlocked = reenteredFuture.wait_for(200ms) == std::future_status::ready;
    });
    const auto third = bus.dispatch(restartCommand("third", "session-3"));
    if (reentryThread.joinable()) {
        reentryThread.join();
    }
    const auto eventuallyReentered = reenteredFuture.wait_for(2s) == std::future_status::ready;
    source.release();
    runtime.stop();
    EXPECT_NE(std::get_if<CommandSuccess>(&first.outcome), nullptr);
    EXPECT_NE(std::get_if<CommandSuccess>(&second.outcome), nullptr);
    EXPECT_NE(std::get_if<CommandSuccess>(&third.outcome), nullptr);
    EXPECT_TRUE(started);
    EXPECT_TRUE(cancellationSeen);
    EXPECT_TRUE(callbackSawUnlocked);
    EXPECT_TRUE(eventuallyReentered);
}

}  // namespace
}  // namespace vna::application
