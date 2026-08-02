#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <variant>
#include <vector>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

enum class CaptureOutcome { Success, CaptureFailure, ProcessingFailure, Canceled };

class ControlledCaptureSource {
public:
    explicit ControlledCaptureSource(
        std::vector<CaptureOutcome> outcomes = {})
        : outcomes_(std::move(outcomes)) {}

    acquisition::RawSweepCaptureResult operator()(
        const acquisition::RawSweepCaptureRequest& request,
        const acquisition::RawSweepChunkObserver& observer,
        std::stop_token token) {
        std::stop_callback notify{token, [this] { notifyWaiters(); }};
        const auto sequence = request.sequenceNumber;
        if (!await(sequence, releasedPreview_, token, true)) {
            return acquisition::RawSweepCaptureCanceled{};
        }
        if (outcome(sequence) == CaptureOutcome::CaptureFailure) {
            return frames::FrameError{frames::FrameErrorCode::NonFiniteSample};
        }
        auto payload = acquisition::test_support::validPayload(sequence);
        if (outcome(sequence) == CaptureOutcome::ProcessingFailure) {
            payload.sourceStates[0].samples[0].reference = {0.0, 0.0};
        }
        const auto& firstSource = payload.sourceStates[0];
        observer({firstSource.sourcePort, 0,
                  {firstSource.samples.cbegin(), firstSource.samples.cbegin() + 2}});
        if (!await(sequence, releasedComplete_, token, false)) {
            return acquisition::RawSweepCaptureCanceled{};
        }
        observer({firstSource.sourcePort, 2,
                  {firstSource.samples.cbegin() + 2, firstSource.samples.cend()}});
        observeSource(payload.sourceStates[1], observer);
        if (outcome(sequence) == CaptureOutcome::Canceled) {
            return acquisition::RawSweepCaptureCanceled{};
        }
        return payload;
    }

    bool waitForRequest(std::uint64_t sequence) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] {
            return requestedSequence_ >= sequence;
        });
    }

    void releasePreview(std::uint64_t sequence) {
        release(releasedPreview_, sequence);
    }

    void releaseComplete(std::uint64_t sequence) {
        release(releasedComplete_, sequence);
    }

private:
    void release(std::uint64_t& released, std::uint64_t sequence) {
        std::lock_guard lock{mutex_};
        released = sequence;
        changed_.notify_all();
    }

    CaptureOutcome outcome(std::uint64_t sequence) const {
        return sequence <= outcomes_.size()
            ? outcomes_[sequence - 1]
            : CaptureOutcome::Success;
    }

    bool await(
        std::uint64_t sequence,
        const std::uint64_t& released,
        std::stop_token token,
        bool announce) {
        std::unique_lock lock{mutex_};
        if (announce) {
            requestedSequence_ = sequence;
            changed_.notify_all();
        }
        changed_.wait(lock, [&] {
            return token.stop_requested() || released >= sequence;
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
    std::vector<CaptureOutcome> outcomes_;
};

StateSnapshot initialState(const FactoryPreset& preset) {
    return {
        0,
        {},
        preset.commandBusState.instrument.snapshot(),
        preset.commandBusState.displayWorkspace.snapshot(),
    };
}

void releaseSweep(ControlledCaptureSource& source, std::uint64_t sequence) {
    EXPECT_TRUE(source.waitForRequest(sequence));
    source.releasePreview(sequence);
    source.releaseComplete(sequence);
}

void seedSequenceOne(
    TracePublicationCatalog& catalog,
    const TracePublicationPlanHandle& plan) {
    const auto& target = plan->targets.front();
    const auto seeded = catalog.publishIfCurrent(
        plan,
        {plan->generation, 1,
         {{frames::FrameId{99}, target.trace.id, target.measurement.id,
           target.measurement.type, plan->stateRevision, plan->generation, 1,
           target.trace.format, {1.0, 2.0},
           CartesianTraceDisplaySamples{
               TraceDisplayUnit::Decibel, {-1.0, -2.0}}}}});
    EXPECT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(seeded));
}

class SweepRuntimeTest : public ::testing::Test {
protected:
    FactoryPreset preset_{makeFactoryPreset()};
    TraceDisplayFrameRepository repository_{4};
    TracePublicationCatalog catalog_{
        preset_.acquisitionChannelId, repository_, initialState(preset_)};
    SweepPreviewExchange previews_;
    OperationManager operations_;
};

TEST_F(SweepRuntimeTest, RecoversThreeStructuredSweepFailures) {
    const auto publication = catalog_.capture();
    seedSequenceOne(catalog_, publication);
    ControlledCaptureSource source{{
        CaptureOutcome::Success, CaptureOutcome::CaptureFailure,
        CaptureOutcome::ProcessingFailure, CaptureOutcome::Success}};
    SweepRuntime runtime{{acquisition::test_support::validPlan(), publication, 2},
                         std::ref(source), previews_, catalog_, operations_};

    releaseSweep(source, 1);
    releaseSweep(source, 2);
    releaseSweep(source, 3);
    releaseSweep(source, 4);
    const auto complete = repository_.waitForNextSet({1, 1});
    ASSERT_TRUE(complete.has_value());
    const auto& published = std::get<FrameSetAvailable>(*complete).frameSet;
    EXPECT_EQ(published->sequenceNumber, 4U);
    EXPECT_EQ(published->frames[0].frameId, frames::FrameId{4});
    runtime.stop();
    const auto snapshot = runtime.snapshot();
    EXPECT_EQ(snapshot.state, SweepRuntimeState::Stopped);
    EXPECT_EQ(snapshot.attemptedSweeps, 4U);
    EXPECT_EQ(snapshot.completedSweeps, 1U);
    EXPECT_EQ(snapshot.rejectedSweeps, 3U);
    ASSERT_TRUE(snapshot.lastSweepFailure.has_value());
    EXPECT_EQ(snapshot.lastSweepFailure->code,
              SweepRuntimeFailureCode::CompleteProcessingFailed);
    EXPECT_EQ(snapshot.lastSweepFailure->attemptedSequence, 3U);
}

TEST_F(SweepRuntimeTest, InvalidatesPreviewBeforeUnexpectedCancelFails) {
    ControlledCaptureSource source{{CaptureOutcome::Canceled}};
    SweepRuntime runtime{{acquisition::test_support::validPlan(),
                          catalog_.capture(), 2},
                         std::ref(source), previews_, catalog_, operations_};

    ASSERT_TRUE(source.waitForRequest(1));
    source.releasePreview(1);
    const auto available = previews_.waitForNext({0});
    ASSERT_TRUE(available.has_value());
    source.releaseComplete(1);
    runtime.join();
    const auto snapshot = runtime.snapshot();
    EXPECT_EQ(snapshot.state, SweepRuntimeState::Failed);
    EXPECT_NE(snapshot.terminalFailure, nullptr);
    const auto invalidated = previews_.waitForNext(
        std::get<SweepPreviewAvailable>(*available).cursor);
    EXPECT_TRUE(std::holds_alternative<SweepPreviewInvalidated>(*invalidated));
}

TEST_F(SweepRuntimeTest, GenerationAdvanceRetiresStalePlan) {
    const auto publication = catalog_.capture();
    ControlledCaptureSource source;
    SweepRuntime runtime{{acquisition::test_support::validPlan(), publication, 2},
                         std::ref(source), previews_, catalog_, operations_};
    ASSERT_TRUE(source.waitForRequest(1));
    source.releasePreview(1);
    ASSERT_TRUE(previews_.waitForNext({0}).has_value());

    auto candidate = initialState(preset_);
    candidate.display.traces[0].format = display_model::TraceFormat::Phase;
    auto prepared = catalog_.prepare(candidate, 1);
    ASSERT_TRUE(std::holds_alternative<PreparedTracePublicationPlan>(prepared));
    const auto advanced = previews_.advanceGeneration(2);
    ASSERT_TRUE(std::holds_alternative<SweepPreviewGenerationAdvanced>(advanced));
    const auto committed = catalog_.commit(
        std::move(std::get<PreparedTracePublicationPlan>(prepared)));
    ASSERT_TRUE(std::holds_alternative<TracePublicationPlanHandle>(committed));

    source.releaseComplete(1);
    runtime.join();
    const auto snapshot = runtime.snapshot();
    EXPECT_EQ(snapshot.state, SweepRuntimeState::Retired);
    EXPECT_EQ(snapshot.previewRejectedSweeps, 1U);
    EXPECT_EQ(repository_.latestFrameSet(), nullptr);
    const auto event = previews_.waitForNext({0});
    EXPECT_TRUE(std::holds_alternative<SweepPreviewGenerationAdvanced>(*event));
}

}  // namespace
}  // namespace vna::application
