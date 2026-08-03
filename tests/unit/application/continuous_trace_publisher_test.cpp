#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stdexcept>
#include <vna/compat/joining_thread.hpp>
#include <thread>
#include <variant>
#include <vector>

#include <vna/application/command_bus.hpp>
#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

StateSnapshot singleTraceState() {
    StateSnapshot state{};
    state.stateRevision = 7;
    state.instrument.channels = {{
        domain::ChannelId{1}, {1'000'000, 2'000'000, 3, 10'000, -10.5}}};
    state.instrument.measurements = {{
        domain::MeasurementId{1}, domain::ChannelId{1},
        domain::MeasurementType::S21}};
    state.display.traces = {{
        display_model::TraceId{1}, display_model::WindowId{1},
        domain::MeasurementId{1}, display_model::TraceFormat::LogMagnitude,
        std::nullopt}};
    return state;
}

class SetWait {
public:
    explicit SetWait(const TraceDisplayFrameRepository& repository)
        : returned_(promise_.get_future()),
          worker_([this, &repository](vna::compat::StopToken token) {
              result_ = repository.waitForNextSet({1, 0}, token);
              promise_.set_value();
          }) {}
    ~SetWait() { worker_.requestStop(); }

    TraceDisplayFrameSetHandle finish() {
        if (returned_.wait_for(2s) != std::future_status::ready) {
            worker_.requestStop();
            throw std::runtime_error{"frame set was not published"};
        }
        worker_.join();
        if (!result_.has_value()) {
            return nullptr;
        }
        const auto* available = std::get_if<FrameSetAvailable>(&*result_);
        return available == nullptr ? nullptr : available->frameSet;
    }

private:
    std::promise<void> promise_;
    std::future<void> returned_;
    std::optional<TraceDisplayFrameSetEvent> result_;
    vna::compat::JoiningThread worker_;
};

class FailingSecondSource {
public:
    frames::Result<frames::RawReceiverPayload> operator()(
        const acquisition::ContinuousAcquisitionPlan& plan,
        std::uint64_t sequence,
        vna::compat::StopToken token) const {
        auto result = source_(plan, sequence, token);
        if (sequence == 2) {
            return frames::Result<frames::RawReceiverPayload>{
                frames::FrameError{frames::FrameErrorCode::InvalidPortCount}};
        }
        return result;
    }
    bool waitForRequest(std::uint64_t sequence) const {
        return source_.waitForRequest(sequence);
    }
    void release(std::uint64_t sequence) { source_.release(sequence); }

private:
    acquisition::test_support::ControlledSource source_;
};

TEST(ContinuousTracePublisherTest, PublishesKnownS21FrameSet) {
    acquisition::test_support::ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, singleTraceState()};
    ContinuousTracePublisher publisher{acquisition, catalog};
    SetWait wait{repository};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);

    const auto set = wait.finish();
    publisher.stop();
    acquisition.stop();

    ASSERT_NE(set, nullptr);
    ASSERT_EQ(set->frames.size(), 1U);
    const auto& frame = set->frames.front();
    EXPECT_EQ(frame.frameId, frames::FrameId{1});
    EXPECT_EQ(frame.traceId, display_model::TraceId{1});
    EXPECT_EQ(frame.measurementId, domain::MeasurementId{1});
    EXPECT_EQ(frame.measurementType, domain::MeasurementType::S21);
    EXPECT_EQ(frame.stateRevision, 7U);
    EXPECT_EQ(frame.generation, 1U);
    EXPECT_EQ(frame.sequenceNumber, 1U);
    EXPECT_EQ(frame.frequenciesHz,
              (std::vector<double>{1'000'000, 1'500'000, 2'000'000}));
    const auto& samples =
        std::get<CartesianTraceDisplaySamples>(frame.samples);
    EXPECT_EQ(samples.unit, TraceDisplayUnit::Decibel);
    EXPECT_NEAR(samples.values.front(), 1.583624921, 1e-9);
}

TEST(ContinuousTracePublisherTest, PreservesLastGoodWhenAcquisitionFails) {
    FailingSecondSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, singleTraceState()};
    ContinuousTracePublisher publisher{acquisition, catalog};
    SetWait wait{repository};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    ASSERT_NE(wait.finish(), nullptr);
    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    acquisition.join();
    publisher.join();

    const auto snapshot = publisher.snapshot();
    EXPECT_EQ(snapshot.state, ContinuousTracePublisherState::AcquisitionFailed);
    EXPECT_EQ(snapshot.observedFrames, 1U);
    EXPECT_EQ(snapshot.publishedFrames, 1U);
    EXPECT_EQ(snapshot.rejectedFrames, 0U);
    EXPECT_EQ(snapshot.lastPublishedSequence, 1U);
    ASSERT_TRUE(snapshot.acquisitionFailure.has_value());
    EXPECT_EQ(snapshot.acquisitionFailure->attemptedSequence, 2U);
    EXPECT_EQ(repository.latestFrameSet()->sequenceNumber, 1U);
}

TEST(ContinuousTracePublisherTest, StopWakesOnlyPublisher) {
    acquisition::test_support::ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, singleTraceState()};
    ContinuousTracePublisher publisher{acquisition, catalog};
    SetWait wait{repository};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    ASSERT_NE(wait.finish(), nullptr);
    ASSERT_TRUE(source.waitForRequest(2));

    publisher.stop();

    EXPECT_EQ(publisher.snapshot().state, ContinuousTracePublisherState::Stopped);
    EXPECT_EQ(acquisition.snapshot().state,
              acquisition::ContinuousAcquisitionState::Running);
    acquisition.stop();
}

}  // namespace
}  // namespace vna::application
