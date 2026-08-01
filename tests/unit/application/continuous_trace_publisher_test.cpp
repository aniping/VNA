#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

class ControlledSource {
public:
    ControlledSource(std::uint64_t failed = 0, std::uint64_t invalid = 0)
        : source_(), failed_(failed), invalid_(invalid) {}
    frames::Result<frames::RawReceiverPayload> operator()(
        const acquisition::ContinuousAcquisitionPlan& plan,
        std::uint64_t sequence,
        std::stop_token token) const {
        auto result = source_(plan, sequence, token);
        if (sequence == failed_) {
            return frames::Result<frames::RawReceiverPayload>{
                frames::FrameError{frames::FrameErrorCode::InvalidPortCount}};
        }
        if (sequence == invalid_) {
            auto payload = result.value();
            payload.sourceStates[0].samples[0].reference = {0.0, 0.0};
            return frames::Result<frames::RawReceiverPayload>{
                std::move(payload)};
        }
        return result;
    }
    bool waitForRequest(std::uint64_t sequence) const {
        return source_.waitForRequest(sequence);
    }
    void release(std::uint64_t sequence) { source_.release(sequence); }
private:
    acquisition::test_support::ControlledSource source_;
    std::uint64_t failed_;
    std::uint64_t invalid_;
};

class PublishProbe {
public:
    PublishProbe(TraceDisplayFrameRepository& repository, bool failFirst = false)
        : repository_(repository), failFirst_(failFirst) {}
    TraceDisplayFrameResult operator()(TraceDisplayFrame frame) const {
        std::size_t call;
        {
            std::lock_guard lock{state_->mutex};
            call = ++state_->calls;
        }
        auto result = failFirst_ && call == 1
            ? TraceDisplayFrameResult{TraceDisplayFrameError{
                  TraceDisplayFrameErrorCode::CapacityExceeded}}
            : repository_.publish(std::move(frame));
        {
            std::lock_guard lock{state_->mutex};
            ++state_->completedCalls;
            state_->changed.notify_all();
        }
        return result;
    }
    bool waitForCompletedCalls(std::size_t calls) const {
        std::unique_lock lock{state_->mutex};
        return state_->changed.wait_for(lock, 2s, [&] {
            return state_->completedCalls >= calls;
        });
    }
private:
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        std::size_t calls{}, completedCalls{};
    };
    TraceDisplayFrameRepository& repository_;
    bool failFirst_;
    std::shared_ptr<State> state_{std::make_shared<State>()};
};

ContinuousTracePreset preset() {
    return {
        .stateRevision = 7,
        .measurement = {
            domain::MeasurementId{1}, domain::ChannelId{1},
            domain::MeasurementType::S21},
        .trace = {
            display_model::TraceId{1}, display_model::WindowId{1},
            domain::MeasurementId{1},
            display_model::TraceFormat::LogMagnitude, std::nullopt},
    };
}

TEST(ContinuousTracePublisherTest, PublishesKnownS21FrameAndCorrelation) {
    ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    PublishProbe probe{repository};
    ContinuousTracePublisher publisher{acquisition, preset(), repository, probe};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    ASSERT_TRUE(probe.waitForCompletedCalls(1));
    publisher.stop();
    acquisition.stop();

    const auto frame = repository.latest(display_model::TraceId{1});
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->frameId, frames::FrameId{1});
    EXPECT_EQ(frame->traceId, display_model::TraceId{1});
    EXPECT_EQ(frame->stateRevision, 7U);
    EXPECT_EQ(frame->sequenceNumber, 1U);
    EXPECT_EQ(frame->format, display_model::TraceFormat::LogMagnitude);
    EXPECT_EQ(frame->measurementId, domain::MeasurementId{1});
    const auto& samples =
        std::get<CartesianTraceDisplaySamples>(frame->samples);
    EXPECT_EQ(samples.unit, TraceDisplayUnit::Decibel);
    EXPECT_EQ(frame->frequenciesHz,
              (std::vector<double>{1'000'000, 1'500'000, 2'000'000}));
    ASSERT_EQ(samples.values.size(), 3U);
    EXPECT_NEAR(samples.values[0], 1.583624921, 1e-9);
}

TEST(ContinuousTracePublisherTest, RejectsOneFrameThenRecovers) {
    ControlledSource source{0, 2};
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    PublishProbe probe{repository};
    ContinuousTracePublisher publisher{acquisition, preset(), repository, probe};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    ASSERT_TRUE(source.waitForRequest(3));
    source.release(3);
    ASSERT_TRUE(probe.waitForCompletedCalls(2));
    publisher.stop();
    acquisition.stop();

    const auto snapshot = publisher.snapshot();
    EXPECT_EQ(snapshot.observedFrames, 3U);
    EXPECT_EQ(snapshot.publishedFrames, 2U);
    EXPECT_EQ(snapshot.rejectedFrames, 1U);
    EXPECT_EQ(snapshot.lastObservedSequence, 3U);
    EXPECT_EQ(snapshot.lastPublishedSequence, 3U);
    EXPECT_EQ(repository.latest(display_model::TraceId{1})->sequenceNumber, 3U);
}

TEST(ContinuousTracePublisherTest, RecoversAfterPublishError) {
    ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    PublishProbe probe{repository, true};
    ContinuousTracePublisher publisher{acquisition, preset(), repository, probe};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    ASSERT_TRUE(probe.waitForCompletedCalls(1));
    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    ASSERT_TRUE(probe.waitForCompletedCalls(2));
    publisher.stop();
    acquisition.stop();

    const auto snapshot = publisher.snapshot();
    EXPECT_EQ(snapshot.publishedFrames, 1U);
    EXPECT_EQ(snapshot.rejectedFrames, 1U);
    EXPECT_EQ(repository.latest(display_model::TraceId{1})->sequenceNumber, 2U);
}

TEST(ContinuousTracePublisherTest, PreservesLastGoodWhenAcquisitionFails) {
    ControlledSource source{2};
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    PublishProbe probe{repository};
    ContinuousTracePublisher publisher{acquisition, preset(), repository, probe};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    acquisition.join();
    publisher.join();

    const auto snapshot = publisher.snapshot();
    EXPECT_EQ(snapshot.state, ContinuousTracePublisherState::AcquisitionFailed);
    EXPECT_EQ(snapshot.observedFrames, 1U);
    EXPECT_EQ(snapshot.publishedFrames, 1U);
    EXPECT_EQ(snapshot.rejectedFrames, 0U);
    EXPECT_EQ(snapshot.lastObservedSequence, 1U);
    EXPECT_EQ(snapshot.lastPublishedSequence, 1U);
    ASSERT_TRUE(snapshot.acquisitionFailure.has_value());
    EXPECT_EQ(
        snapshot.acquisitionFailure->code,
        acquisition::ContinuousAcquisitionFailureCode::SourceFailed);
    EXPECT_EQ(snapshot.acquisitionFailure->attemptedSequence, 2U);
    EXPECT_EQ(repository.latest(display_model::TraceId{1})->sequenceNumber, 1U);
}

TEST(ContinuousTracePublisherTest, StopWakesOnlyPublisher) {
    ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    PublishProbe probe{repository};
    ContinuousTracePublisher publisher{acquisition, preset(), repository, probe};
    const auto firstRequested = source.waitForRequest(1);
    source.release(1);
    const auto firstPublished = probe.waitForCompletedCalls(1);
    const auto secondRequested = source.waitForRequest(2);

    publisher.stop();

    EXPECT_TRUE(firstRequested);
    EXPECT_TRUE(firstPublished);
    EXPECT_TRUE(secondRequested);
    EXPECT_EQ(publisher.snapshot().state, ContinuousTracePublisherState::Stopped);
    EXPECT_EQ(acquisition.snapshot().state,
              acquisition::ContinuousAcquisitionState::Running);
    acquisition.stop();
}
}  // namespace
}  // namespace vna::application
