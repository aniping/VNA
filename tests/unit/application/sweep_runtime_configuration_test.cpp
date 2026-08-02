#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

class BlockingSource {
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
        return changed_.wait_for(lock, 2s, [&] {
            return requested_ >= sequence;
        });
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

StateSnapshot presetState(const FactoryPreset& preset) {
    return {
        0,
        {},
        preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot(),
    };
}

class SweepRuntimeConfigurationTest : public ::testing::Test {
protected:
    StateSnapshot phaseCandidate() const {
        auto candidate = initial_;
        candidate.stateRevision = 1;
        candidate.display.traces[0].format =
            display_model::TraceFormat::Phase;
        return candidate;
    }

    FactoryPreset preset_{makeFactoryPreset()};
    StateSnapshot initial_{presetState(preset_)};
    TraceDisplayFrameRepository repository_{4};
    TracePublicationCatalog catalog_{
        preset_.acquisitionChannelId, repository_, initial_};
    SweepPreviewExchange previews_;
    OperationManager operations_;
};

TEST_F(SweepRuntimeConfigurationTest,
       AbandonedPreparationDoesNotBlockAtomicNextBoundary) {
    BlockingSource source;
    SweepRuntime runtime{{acquisition::test_support::validPlan(),
                          catalog_.capture(), 2},
                         std::ref(source), previews_, catalog_, operations_};
    ASSERT_TRUE(source.waitFor(1));
    auto candidate = phaseCandidate();
    {
        auto abandoned = runtime.prepareConfiguration(candidate);
        ASSERT_TRUE(std::holds_alternative<
                    PreparedSweepRuntimeConfiguration>(abandoned));
    }
    auto prepared = runtime.prepareConfiguration(candidate);
    ASSERT_TRUE(std::holds_alternative<
                PreparedSweepRuntimeConfiguration>(prepared));

    runtime.commitConfiguration(std::get<
        PreparedSweepRuntimeConfiguration>(std::move(prepared)));
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    source.release(1);
    ASSERT_TRUE(source.waitFor(2));

    EXPECT_EQ(catalog_.capture()->generation, 2U);
    const auto event = previews_.waitForNext({0});
    ASSERT_TRUE(event.has_value());
    EXPECT_TRUE(std::holds_alternative<
                SweepPreviewGenerationAdvanced>(*event));
    runtime.stop();
}

}  // namespace
}  // namespace vna::application
