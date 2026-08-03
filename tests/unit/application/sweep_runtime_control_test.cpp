#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <vna/compat/stop_token.hpp>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>
#include <vna/test/sweep_status_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

OperationSnapshot waitForTerminal(
    OperationManager& operations,
    const OperationSnapshot& accepted) {
    std::promise<void> completed;
    auto future = completed.get_future();
    auto fence = operations.captureFence(accepted.sessionId);
    auto subscription = operations.subscribe(
        std::move(fence), [&] { completed.set_value(); });
    EXPECT_EQ(future.wait_for(2s), std::future_status::ready);
    return std::get<OperationSnapshot>(operations.snapshot(accepted.id));
}

acquisition::RawSweepCaptureResult validSource(
    const acquisition::RawSweepCaptureRequest& request,
    const acquisition::RawSweepChunkObserver&,
    vna::compat::StopToken) {
    return acquisition::test_support::validPayload(request.sequenceNumber);
}

class BlockingCaptureSource {
public:
    acquisition::RawSweepCaptureResult operator()(
        const acquisition::RawSweepCaptureRequest& request,
        const acquisition::RawSweepChunkObserver&,
        vna::compat::StopToken token) {
        if (request.sequenceNumber == 2) {
            return acquisition::test_support::validPayload(
                request.sequenceNumber);
        }
        vna::compat::StopCallback notify{
            token, [this] { announceCancellation(); }};
        std::unique_lock lock{mutex_};
        started_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] {
            return token.stopRequested() && releaseCancellation_;
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

    void releaseCancellation() {
        std::lock_guard lock{mutex_};
        releaseCancellation_ = true;
        changed_.notify_all();
    }

private:
    void announceCancellation() {
        std::lock_guard lock{mutex_};
        cancellationSeen_ = true;
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    bool started_{};
    bool cancellationSeen_{};
    bool releaseCancellation_{};
};

class SweepRuntimeControlTest : public ::testing::Test {
protected:
    FactoryPreset preset_{makeFactoryPreset()};
    TraceDisplayFrameRepository repository_{4};
    TracePublicationCatalog catalog_{
        preset_.acquisitionChannelId, repository_,
        {0, {}, preset_.commandBusState.instrument.snapshot(),
         preset_.commandBusState.displayWorkspace.snapshot()}};
    SweepPreviewExchange previews_{vna::test::testSweepStatus()};
    OperationManager operations_;
};

TEST_F(SweepRuntimeControlTest, SingleRestartCompletesOnlyPublishedFrame) {
    SweepRuntime runtime{
        {acquisition::test_support::validPlan(), catalog_.capture(), 2,
         {domain::SweepMode::Single, 1}},
        validSource, previews_, catalog_, operations_};
    EXPECT_EQ(runtime.snapshot().phase, SweepUserPhase::Hold);
    EXPECT_EQ(runtime.snapshot().progress, (SweepAcquisitionProgress{6, 6}));

    const auto submitted = runtime.requestRestart(domain::ChannelId{1}, {
        CommandId{"restart-1"}, SessionId{"session-1"}, 7});
    ASSERT_TRUE(std::holds_alternative<OperationId>(submitted));
    const auto operationId = std::get<OperationId>(submitted);
    const auto accepted =
        std::get<OperationSnapshot>(operations_.snapshot(operationId));
    const auto terminal = waitForTerminal(operations_, accepted);

    const auto* succeeded = std::get_if<OperationSucceeded>(&terminal.state);
    ASSERT_NE(succeeded, nullptr);
    EXPECT_EQ(succeeded->frameId, frames::FrameId{1});
    const auto published = repository_.latestFrameSet();
    ASSERT_NE(published, nullptr);
    EXPECT_EQ(published->frames.front().frameId, succeeded->frameId);
    EXPECT_EQ(runtime.snapshot().phase, SweepUserPhase::Hold);
    runtime.stop();
}

TEST_F(SweepRuntimeControlTest, SingleCompletesAfterConfiguredSweepCount) {
    SweepRuntime runtime{
        {acquisition::test_support::validPlan(), catalog_.capture(), 2,
         {domain::SweepMode::Single, 3}},
        validSource, previews_, catalog_, operations_};

    const auto submitted = runtime.requestRestart(domain::ChannelId{1}, {
        CommandId{"restart-3"}, SessionId{"session-1"}, 9});
    ASSERT_TRUE(std::holds_alternative<OperationId>(submitted));
    const auto operationId = std::get<OperationId>(submitted);
    const auto accepted =
        std::get<OperationSnapshot>(operations_.snapshot(operationId));
    const auto terminal = waitForTerminal(operations_, accepted);

    const auto* succeeded = std::get_if<OperationSucceeded>(&terminal.state);
    ASSERT_NE(succeeded, nullptr);
    EXPECT_EQ(succeeded->frameId, frames::FrameId{3});
    EXPECT_EQ(runtime.snapshot().completedSweeps, 3U);
    EXPECT_EQ(runtime.snapshot().phase, SweepUserPhase::Hold);
    runtime.stop();
}

TEST_F(SweepRuntimeControlTest, ReplacementCancelsQueuedBeforeActiveSource) {
    BlockingCaptureSource source;
    SweepRuntime runtime{
        {acquisition::test_support::validPlan(), catalog_.capture(), 2,
         {domain::SweepMode::Single, 1}},
        std::ref(source), previews_, catalog_, operations_};
    const auto first = std::get<OperationId>(runtime.requestRestart(
        domain::ChannelId{1}, {
        CommandId{"restart-a"}, SessionId{"session-1"}, 1}));
    EXPECT_TRUE(source.waitForStart());
    const auto queued = std::get<OperationId>(runtime.requestRestart(
        domain::ChannelId{1}, {
        CommandId{"restart-b"}, SessionId{"session-1"}, 1}));
    const auto newest = std::get<OperationId>(runtime.requestRestart(
        domain::ChannelId{1}, {
        CommandId{"restart-c"}, SessionId{"session-1"}, 1}));

    EXPECT_TRUE(source.waitForCancellation());
    EXPECT_TRUE(std::holds_alternative<OperationCancelRequested>(
        std::get<OperationSnapshot>(operations_.snapshot(first)).state));
    EXPECT_TRUE(std::holds_alternative<OperationCanceled>(
        std::get<OperationSnapshot>(operations_.snapshot(queued)).state));
    EXPECT_TRUE(std::holds_alternative<OperationQueued>(
        std::get<OperationSnapshot>(operations_.snapshot(newest)).state));
    source.releaseCancellation();
    const auto canceled = waitForTerminal(
        operations_, std::get<OperationSnapshot>(operations_.snapshot(first)));
    EXPECT_TRUE(std::holds_alternative<OperationCanceled>(canceled.state));
    runtime.stop();
}

TEST_F(SweepRuntimeControlTest, SingleFailureKeepsFailedUntilRestart) {
    auto plan = acquisition::test_support::validPlan();
    plan.minimumSweepPeriod = 100ms;
    const auto source = [](
                            const acquisition::RawSweepCaptureRequest&,
                            const acquisition::RawSweepChunkObserver&,
                            vna::compat::StopToken) ->
        acquisition::RawSweepCaptureResult {
        return frames::FrameError{frames::FrameErrorCode::NonFiniteSample};
    };
    SweepRuntime runtime{{plan, catalog_.capture(), 2,
                          {domain::SweepMode::Single, 1}},
                         source, previews_, catalog_, operations_};
    const auto operationId = std::get<OperationId>(runtime.requestRestart(
        domain::ChannelId{1}, {
        CommandId{"restart-fail"}, SessionId{"session-1"}, 4}));
    const auto terminal = waitForTerminal(
        operations_, std::get<OperationSnapshot>(
                         operations_.snapshot(operationId)));

    EXPECT_TRUE(std::holds_alternative<OperationFailed>(terminal.state));
    EXPECT_EQ(runtime.snapshot().phase, SweepUserPhase::Failed);
    runtime.stop();
}

TEST_F(SweepRuntimeControlTest, ContinuousRestartUsesReplacementSweep) {
    BlockingCaptureSource source;
    SweepRuntime runtime{{acquisition::test_support::validPlan(),
                          catalog_.capture(), 2},
                         std::ref(source), previews_, catalog_, operations_};
    EXPECT_TRUE(source.waitForStart());

    const auto operationId = std::get<OperationId>(runtime.requestRestart(
        domain::ChannelId{1}, {
        CommandId{"restart-continuous"}, SessionId{"session-1"}, 3}));
    const auto cancellationSeen = source.waitForCancellation();
    source.releaseCancellation();
    const auto accepted =
        std::get<OperationSnapshot>(operations_.snapshot(operationId));
    const auto terminal = waitForTerminal(operations_, accepted);

    const auto* succeeded = std::get_if<OperationSucceeded>(&terminal.state);
    ASSERT_NE(succeeded, nullptr);
    EXPECT_EQ(succeeded->frameId, frames::FrameId{2});
    EXPECT_EQ(runtime.snapshot().completedSweeps, 1U);
    EXPECT_TRUE(cancellationSeen);
    runtime.stop();
}

}  // namespace
}  // namespace vna::application
