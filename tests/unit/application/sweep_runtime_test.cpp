#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

class ControlledCaptureSource {
public:
    acquisition::RawSweepCaptureResult operator()(
        const acquisition::RawSweepCaptureRequest& request,
        const acquisition::RawSweepChunkObserver& observer,
        std::stop_token token) {
        std::stop_callback notify{token, [this] { notifyWaiters(); }};
        const auto sequence = request.sequenceNumber;
        if (!await(sequence, token)) {
            return acquisition::RawSweepCaptureCanceled{};
        }
        const auto payload = acquisition::test_support::validPayload(sequence);
        observeSource(payload.sourceStates[0], observer);
        if (!awaitCompletion(sequence, token)) {
            return acquisition::RawSweepCaptureCanceled{};
        }
        observeSource(payload.sourceStates[1], observer);
        return payload;
    }

    bool waitForRequest(std::uint64_t sequence) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] {
            return requestedSequence_ >= sequence;
        });
    }

    void releasePreview(std::uint64_t sequence) {
        std::lock_guard lock{mutex_};
        releasedPreview_ = sequence;
        changed_.notify_all();
    }

    void releaseComplete(std::uint64_t sequence) {
        std::lock_guard lock{mutex_};
        releasedComplete_ = sequence;
        changed_.notify_all();
    }

private:
    bool await(std::uint64_t sequence, std::stop_token token) {
        std::unique_lock lock{mutex_};
        requestedSequence_ = sequence;
        changed_.notify_all();
        changed_.wait(lock, [&] {
            return token.stop_requested() || releasedPreview_ >= sequence;
        });
        return !token.stop_requested();
    }

    bool awaitCompletion(std::uint64_t sequence, std::stop_token token) {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [&] {
            return token.stop_requested() || releasedComplete_ >= sequence;
        });
        return !token.stop_requested();
    }

    void notifyWaiters() {
        std::lock_guard lock{mutex_};
        changed_.notify_all();
    }

    static void observeSource(
        const frames::RawSourceState& source,
        const acquisition::RawSweepChunkObserver& observer) {
        observer({source.sourcePort, 0, {source.samples.cbegin(),
                                        source.samples.cbegin() + 2}});
        observer({source.sourcePort, 2, {source.samples.cbegin() + 2,
                                        source.samples.cend()}});
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t requestedSequence_{};
    std::uint64_t releasedPreview_{};
    std::uint64_t releasedComplete_{};
};

StateSnapshot initialState(const FactoryPreset& preset) {
    return {
        0,
        {},
        preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot(),
    };
}

TEST(SweepRuntimeTest, PublishesPreviewThenOneCompleteFrameSet) {
    auto preset = makeFactoryPreset();
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{
        preset.acquisitionChannelId, repository, initialState(preset)};
    SweepPreviewExchange previews;
    ControlledCaptureSource source;
    auto plan = acquisition::test_support::validPlan();
    plan.minimumSweepPeriod = 1s;
    SweepRuntime runtime{
        SweepRuntimePlan{plan, catalog.capture(), 2},
        std::ref(source), previews, catalog};

    ASSERT_TRUE(source.waitForRequest(1));
    source.releasePreview(1);
    const auto event = previews.waitForNext({0});
    ASSERT_TRUE(event.has_value());
    const auto* available = std::get_if<SweepPreviewAvailable>(&*event);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(available->preview->identity.sweepId, acquisition::SweepId{1});
    EXPECT_GT(available->preview->traces[0].frequenciesHz.size(), 0U);
    EXPECT_LE(available->preview->traces[0].frequenciesHz.size(), 3U);

    source.releaseComplete(1);
    const auto complete = repository.waitForNextSet({1, 0});
    ASSERT_TRUE(complete.has_value());
    const auto* published = std::get_if<FrameSetAvailable>(&*complete);
    ASSERT_NE(published, nullptr);
    EXPECT_EQ(published->frameSet->sequenceNumber, 1U);
    EXPECT_EQ(published->frameSet->frames[0].frameId, frames::FrameId{1});
    runtime.stop();
    const auto snapshot = runtime.snapshot();
    EXPECT_EQ(snapshot.state, SweepRuntimeState::Stopped);
    EXPECT_EQ(snapshot.attemptedSweeps, 1U);
    EXPECT_EQ(snapshot.completedSweeps, 1U);
    EXPECT_EQ(snapshot.rejectedSweeps, 0U);
    EXPECT_EQ(snapshot.previewRejectedSweeps, 0U);
}

}  // namespace
}  // namespace vna::application
