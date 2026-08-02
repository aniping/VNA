#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
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
        requestedPlan_ = request.plan;
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

StateSnapshot presetState(const FactoryPreset& preset) {
    return {
        0,
        {},
        preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot(),
    };
}

StateSnapshot runtimeState(const FactoryPreset& preset) {
    auto state = presetState(preset);
    const auto plan = acquisition::test_support::validPlan();
    auto& sweep = state.instrument.channels[0].sweep;
    sweep.startFrequencyHz = plan.frequencyAxis.startFrequencyHz;
    sweep.stopFrequencyHz = plan.frequencyAxis.stopFrequencyHz;
    sweep.points = plan.frequencyAxis.points;
    sweep.ifBandwidthHz = plan.ifBandwidthHz;
    sweep.powerDbm = plan.powerDbm;
    return state;
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

    void stage(SweepRuntime& runtime, StateSnapshot candidate) {
        auto prepared = runtime.prepareConfiguration(candidate);
        ASSERT_TRUE(std::holds_alternative<
                    PreparedSweepRuntimeConfiguration>(prepared));
        runtime.commitConfiguration(std::get<
            PreparedSweepRuntimeConfiguration>(std::move(prepared)));
    }

    FactoryPreset preset_{makeFactoryPreset()};
    StateSnapshot initial_{runtimeState(preset_)};
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

TEST_F(SweepRuntimeConfigurationTest,
       AcquisitionChangeAdvancesGenerationAtNextBoundary) {
    BlockingSource source;
    SweepRuntime runtime{{acquisition::test_support::validPlan(),
                          catalog_.capture(), 2},
                         std::ref(source), previews_, catalog_, operations_};
    ASSERT_TRUE(source.waitFor(1));
    auto candidate = initial_;
    candidate.stateRevision = 1;
    candidate.instrument.channels[0].sweep.startFrequencyHz = 1'200'000;

    stage(runtime, candidate);
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    EXPECT_EQ(runtime.snapshot().appliedStateRevision, 0U);
    EXPECT_EQ(runtime.snapshot().appliedGeneration, 1U);
    source.release(1);
    ASSERT_TRUE(source.waitFor(2));

    EXPECT_EQ(catalog_.capture()->generation, 2U);
    EXPECT_EQ(catalog_.capture()->stateRevision, 1U);
    EXPECT_EQ(runtime.snapshot().appliedStateRevision, 1U);
    EXPECT_EQ(runtime.snapshot().appliedGeneration, 2U);
    EXPECT_EQ(
        source.requestedPlan().frequencyAxis.startFrequencyHz,
        1'200'000U);
    runtime.stop();
}

TEST_F(SweepRuntimeConfigurationTest,
       LatestPendingCandidateReplacesEarlierCandidate) {
    BlockingSource source;
    SweepRuntime runtime{{acquisition::test_support::validPlan(),
                          catalog_.capture(), 2},
                         std::ref(source), previews_, catalog_, operations_};
    ASSERT_TRUE(source.waitFor(1));
    auto first = initial_;
    first.stateRevision = 1;
    first.instrument.channels[0].sweep.stopFrequencyHz = 3'000'000;
    stage(runtime, first);
    auto latest = phaseCandidate();
    latest.stateRevision = 2;
    latest.instrument.channels[0].sweep.stopFrequencyHz = 4'000'000;
    stage(runtime, latest);

    source.release(1);
    ASSERT_TRUE(source.waitFor(2));

    const auto applied = catalog_.capture();
    EXPECT_EQ(applied->generation, 2U);
    EXPECT_EQ(applied->stateRevision, 2U);
    EXPECT_EQ(applied->targets[0].trace.format,
              display_model::TraceFormat::Phase);
    EXPECT_EQ(source.requestedPlan().frequencyAxis.stopFrequencyHz,
              4'000'000U);
    runtime.stop();
}

TEST_F(SweepRuntimeConfigurationTest,
       RejectedPreparationLeavesAllGenerationsUntouched) {
    BlockingSource source;
    SweepRuntime runtime{{acquisition::test_support::validPlan(),
                          catalog_.capture(), 2},
                         std::ref(source), previews_, catalog_, operations_};
    ASSERT_TRUE(source.waitFor(1));
    auto invalid = initial_;
    invalid.stateRevision = 1;
    invalid.display.traces[0].measurementId = domain::MeasurementId{99};

    const auto rejected = runtime.prepareConfiguration(invalid);

    ASSERT_TRUE(std::holds_alternative<
                SweepRuntimeConfigurationError>(rejected));
    EXPECT_EQ(catalog_.capture()->generation, 1U);
    runtime.stop();
    EXPECT_TRUE(std::holds_alternative<GenerationAdvanced>(
        repository_.advanceGeneration(2)));
    EXPECT_TRUE(std::holds_alternative<SweepPreviewGenerationAdvanced>(
        previews_.advanceGeneration(2)));
}

TEST_F(SweepRuntimeConfigurationTest,
       DisplayScaleRevisionDoesNotAdvanceGeneration) {
    BlockingSource source;
    SweepRuntime runtime{{acquisition::test_support::validPlan(),
                          catalog_.capture(), 2},
                         std::ref(source), previews_, catalog_, operations_};
    ASSERT_TRUE(source.waitFor(1));
    auto candidate = initial_;
    candidate.stateRevision = 1;
    candidate.display.traces[0].scale->scalePerDivision = 5.0;

    stage(runtime, candidate);
    source.release(1);
    ASSERT_TRUE(source.waitFor(2));

    EXPECT_EQ(catalog_.capture()->generation, 1U);
    EXPECT_EQ(runtime.snapshot().appliedStateRevision, 1U);
    EXPECT_EQ(runtime.snapshot().appliedGeneration, 1U);
    runtime.stop();
}

}  // namespace
}  // namespace vna::application
