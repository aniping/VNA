#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

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
        requestedPlan_ = request.plan;
        changed_.notify_all();
        changed_.wait(lock, [&] {
            return token.stop_requested() || released_ >= requested_;
        });
        if (token.stop_requested()) {
            return acquisition::RawSweepCaptureCanceled{};
        }
        return acquisition::test_support::validPayload(requested_);
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

    acquisition::ContinuousAcquisitionPlan requestedPlan() const {
        std::lock_guard lock{mutex_};
        return *requestedPlan_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t requested_{};
    std::uint64_t released_{};
    std::optional<acquisition::ContinuousAcquisitionPlan> requestedPlan_;
};

StateSnapshot initialState(const FactoryPreset& preset) {
    StateSnapshot state{
        7, {}, preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot()};
    const auto plan = acquisition::test_support::validPlan();
    auto& channel = state.instrument.channels[0];
    auto& sweep = channel.sweep;
    sweep.startFrequencyHz = plan.frequencyAxis.startFrequencyHz;
    sweep.stopFrequencyHz = plan.frequencyAxis.stopFrequencyHz;
    sweep.points = plan.frequencyAxis.points;
    sweep.ifBandwidthHz = plan.ifBandwidthHz;
    sweep.powerDbm = plan.powerDbm;
    channel.sweepMode = domain::SweepMode::Single;
    channel.sweepCount = 2;
    return state;
}

TEST(SweepRuntimeOperationRevisionTest,
     MultiSweepOperationRetainsAdmissionRevisionAcrossAppliedPlans) {
    const auto preset = makeFactoryPreset();
    const auto initial = initialState(preset);
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{
        preset.acquisitionChannelId, repository, initial};
    SweepPreviewExchange previews;
    OperationManager operations;
    BoundarySource source;
    SweepRuntime runtime{
        {acquisition::test_support::validPlan(), catalog.capture(), 2,
         {domain::SweepMode::Single, 2}},
        std::ref(source), previews, catalog, operations};

    const auto operationId = std::get<OperationId>(runtime.requestRestart({
        CommandId{"restart-two-plans"}, SessionId{"session-1"}, 7}));
    auto fence = operations.captureFence(SessionId{"session-1"});
    std::promise<void> completed;
    auto completedFuture = completed.get_future();
    auto subscription = operations.subscribe(
        std::move(fence), [&] { completed.set_value(); });
    ASSERT_TRUE(source.waitFor(1));
    EXPECT_EQ(source.requestedPlan().frequencyAxis.stopFrequencyHz, 2'000'000U);

    auto candidate = initial;
    candidate.stateRevision = 8;
    candidate.instrument.channels[0].sweep.stopFrequencyHz = 3'000'000;
    auto prepared = runtime.prepareConfiguration(candidate);
    ASSERT_TRUE(std::holds_alternative<
                PreparedSweepRuntimeConfiguration>(prepared));
    runtime.commitConfiguration(std::get<PreparedSweepRuntimeConfiguration>(
        std::move(prepared)));
    EXPECT_EQ(runtime.snapshot().configuredStateRevision, 8U);
    EXPECT_EQ(runtime.snapshot().appliedStateRevision, 7U);

    source.release(1);
    ASSERT_TRUE(source.waitFor(2));
    EXPECT_EQ(source.requestedPlan().frequencyAxis.stopFrequencyHz, 3'000'000U);
    EXPECT_EQ(runtime.snapshot().appliedStateRevision, 8U);
    EXPECT_EQ(runtime.snapshot().appliedGeneration, 2U);
    source.release(2);
    ASSERT_EQ(completedFuture.wait_for(2s), std::future_status::ready);

    const auto terminal = std::get<OperationSnapshot>(
        operations.snapshot(operationId));
    EXPECT_EQ(terminal.submittedAtStateRevision, 7U);
    const auto* succeeded = std::get_if<OperationSucceeded>(&terminal.state);
    ASSERT_NE(succeeded, nullptr);
    EXPECT_EQ(succeeded->frameId, frames::FrameId{2});
    const auto published = repository.latestFrameSet();
    ASSERT_NE(published, nullptr);
    EXPECT_EQ(published->generation, 2U);
    EXPECT_EQ(published->frames.front().stateRevision, 8U);
    EXPECT_EQ(runtime.snapshot().completedSweeps, 2U);
    EXPECT_EQ(runtime.snapshot().phase, SweepRuntimePhase::Hold);
    runtime.stop();
}

}  // namespace
}  // namespace vna::application
