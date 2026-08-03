#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

class StatusCaptureSource {
public:
    explicit StatusCaptureSource(bool failAfterFirst = false)
        : failAfterFirst_(failAfterFirst) {}

    acquisition::RawSweepCaptureResult operator()(
        const acquisition::RawSweepCaptureRequest& request,
        const acquisition::RawSweepChunkObserver& observer,
        std::stop_token token) {
        std::stop_callback notify{token, [this] { notifyWaiters(); }};
        auto payload = acquisition::test_support::validPayload(
            request.sequenceNumber);
        if (!await(request.sequenceNumber, releaseFirst_, token, true)) {
            return acquisition::RawSweepCaptureCanceled{};
        }
        const auto& first = payload.sourceStates[0];
        observer({first.sourcePort, 0,
                  {first.samples.cbegin(), first.samples.cbegin() + 2}});
        announceFirstRange(request.sequenceNumber);
        if (!await(request.sequenceNumber, releaseComplete_, token, false)) {
            return acquisition::RawSweepCaptureCanceled{};
        }
        if (failAfterFirst_) {
            return frames::FrameError{frames::FrameErrorCode::NonFiniteSample};
        }
        observer({first.sourcePort, 2,
                  {first.samples.cbegin() + 2, first.samples.cend()}});
        const auto& second = payload.sourceStates[1];
        observer({second.sourcePort, 0,
                  {second.samples.cbegin(), second.samples.cend()}});
        return payload;
    }

    bool waitForRequest(std::uint64_t sequence = 1) {
        return waitFor(requested_, sequence);
    }
    bool waitForFirstRange(std::uint64_t sequence = 1) {
        return waitFor(firstRangeObserved_, sequence);
    }
    void releaseFirst(std::uint64_t sequence = 1) {
        release(releaseFirst_, sequence);
    }
    void releaseComplete(std::uint64_t sequence = 1) {
        release(releaseComplete_, sequence);
    }

private:
    bool await(
        std::uint64_t sequence,
        const std::uint64_t& gate,
        std::stop_token token,
        bool announce) {
        std::unique_lock lock{mutex_};
        requested_ = announce ? sequence : requested_;
        changed_.notify_all();
        changed_.wait(lock, [&] {
            return gate >= sequence || token.stop_requested();
        });
        return !token.stop_requested();
    }

    bool waitFor(const std::uint64_t& value, std::uint64_t sequence) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] { return value >= sequence; });
    }

    void release(std::uint64_t& gate, std::uint64_t sequence) {
        std::lock_guard lock{mutex_};
        gate = sequence;
        changed_.notify_all();
    }

    void announceFirstRange(std::uint64_t sequence) {
        std::lock_guard lock{mutex_};
        firstRangeObserved_ = sequence;
        changed_.notify_all();
    }

    void notifyWaiters() {
        std::lock_guard lock{mutex_};
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    bool failAfterFirst_{};
    std::uint64_t requested_{};
    std::uint64_t firstRangeObserved_{};
    std::uint64_t releaseFirst_{};
    std::uint64_t releaseComplete_{};
};

StateSnapshot initialState(const FactoryPreset& preset) {
    return {0, {}, preset.commandBusState.instrument.snapshot(),
            preset.commandBusState.displayWorkspace.snapshot()};
}

class SweepRuntimeStatusTest : public ::testing::Test {
protected:
    SweepRuntimePlan plan() {
        auto acquisition = acquisition::test_support::validPlan();
        acquisition.minimumSweepPeriod = 1h;
        return {std::move(acquisition), catalog_.capture(), 2};
    }

    FactoryPreset preset_{makeFactoryPreset()};
    TraceDisplayFrameRepository repository_{4};
    TracePublicationCatalog catalog_{
        preset_.acquisitionChannelId, repository_, initialState(preset_)};
    OperationManager operations_;
};

TEST_F(SweepRuntimeStatusTest, ReportsAuthoritativeCompleteSweepProgress) {
    auto runtimePlan = plan();
    SweepPreviewExchange previews{initialSweepRuntimeStatus(runtimePlan)};
    StatusCaptureSource source;
    SweepRuntime runtime{
        std::move(runtimePlan), std::ref(source), previews, catalog_, operations_};
    ASSERT_TRUE(source.waitForRequest());
    const auto preparing = previews.waitForNext({1});
    ASSERT_TRUE(preparing.has_value());
    const auto& preparingStatus = std::visit(
        [](const auto& event) -> const SweepPreviewStreamStatus& {
            return event.status;
        }, *preparing);
    EXPECT_EQ(preparingStatus.runtime.userPhase, SweepUserPhase::Preparing);
    EXPECT_EQ(preparingStatus.runtime.progress,
              (SweepAcquisitionProgress{0, 6}));

    source.releaseFirst();
    ASSERT_TRUE(source.waitForFirstRange());
    const auto sweeping = previews.waitForNext(
        std::visit([](const auto& event) { return event.cursor; }, *preparing));
    ASSERT_TRUE(sweeping.has_value());
    const auto* available = std::get_if<SweepPreviewAvailable>(&*sweeping);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(available->status.runtime.userPhase, SweepUserPhase::Sweeping);
    EXPECT_EQ(available->status.runtime.progress,
              (SweepAcquisitionProgress{2, 6}));

    source.releaseComplete();
    ASSERT_TRUE(repository_.waitForNextSet({1, 0}).has_value());
    const auto completed = previews.waitForNext(available->cursor);
    ASSERT_TRUE(completed.has_value());
    const auto& completedStatus = std::visit(
        [](const auto& event) -> const SweepPreviewStreamStatus& {
            return event.status;
        }, *completed);
    EXPECT_EQ(completedStatus.runtime.userPhase, SweepUserPhase::Preparing);
    EXPECT_EQ(completedStatus.runtime.progress,
              (SweepAcquisitionProgress{0, 6}));
    EXPECT_EQ(completedStatus.activePreviewIdentity, std::nullopt);
    runtime.stop();
}

TEST_F(SweepRuntimeStatusTest, FailureKeepsProgressAtFailurePoint) {
    auto runtimePlan = plan();
    SweepPreviewExchange previews{initialSweepRuntimeStatus(runtimePlan)};
    StatusCaptureSource source{true};
    SweepRuntime runtime{
        std::move(runtimePlan), std::ref(source), previews, catalog_, operations_};
    ASSERT_TRUE(source.waitForRequest());
    source.releaseFirst();
    ASSERT_TRUE(source.waitForFirstRange());
    const auto sweeping = previews.waitForNext({1});
    ASSERT_TRUE(sweeping.has_value());
    source.releaseComplete();

    const auto event = previews.waitForNext(std::visit(
        [](const auto& value) { return value.cursor; }, *sweeping));
    ASSERT_TRUE(event.has_value());
    const auto& status = std::visit(
        [](const auto& value) -> const SweepPreviewStreamStatus& {
            return value.status;
        }, *event);
    EXPECT_EQ(status.runtime.userPhase, SweepUserPhase::Failed);
    EXPECT_EQ(status.runtime.progress, (SweepAcquisitionProgress{2, 6}));
    EXPECT_EQ(status.activePreviewIdentity, std::nullopt);
    runtime.stop();
}

}  // namespace
}  // namespace vna::application
