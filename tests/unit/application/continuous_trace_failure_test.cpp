#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <stdexcept>
#include <vna/compat/joining_thread.hpp>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

StateSnapshot publicationState(
    std::size_t traceCount,
    display_model::TraceFormat format =
        display_model::TraceFormat::LogMagnitude) {
    StateSnapshot state{
        .stateRevision = 7,
        .control = {},
        .instrument = {
            .channels = {{
                .id = domain::ChannelId{1},
                .sweep = {1'000'000, 2'000'000, 3, 10'000, -10.5}}},
        },
        .display = {},
    };
    for (std::size_t index = 0; index < traceCount; ++index) {
        const auto id = static_cast<std::uint64_t>(index + 1);
        state.instrument.measurements.push_back({
            domain::MeasurementId{id}, domain::ChannelId{1},
            domain::MeasurementType::S21});
        state.display.traces.push_back({
            display_model::TraceId{id}, display_model::WindowId{1},
            domain::MeasurementId{id},
            format, std::nullopt});
    }
    return state;
}

auto finiteSource() {
    return [](const acquisition::ContinuousAcquisitionPlan&,
              std::uint64_t sequence,
              vna::compat::StopToken) {
        if (sequence != 1) {
            return frames::Result<frames::RawReceiverPayload>{
                frames::FrameError{frames::FrameErrorCode::InvalidPortCount}};
        }
        auto payload = acquisition::test_support::validPayload(sequence);
        return frames::Result<frames::RawReceiverPayload>{std::move(payload)};
    };
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

class ProjectionFailureSource {
public:
    frames::Result<frames::RawReceiverPayload> operator()(
        const acquisition::ContinuousAcquisitionPlan& plan,
        std::uint64_t sequence,
        vna::compat::StopToken token) const {
        auto result = source_(plan, sequence, token);
        if (sequence == 3) {
            return frames::Result<frames::RawReceiverPayload>{
                frames::FrameError{frames::FrameErrorCode::InvalidPortCount}};
        }
        auto payload = result.value();
        if (sequence == 2) {
            payload.sourceStates.front().samples.front().reference = {};
        }
        return frames::Result<frames::RawReceiverPayload>{std::move(payload)};
    }
    bool waitForRequest(std::uint64_t sequence) const {
        return source_.waitForRequest(sequence);
    }
    void release(std::uint64_t sequence) const { source_.release(sequence); }

private:
    acquisition::test_support::ControlledSource source_;
};

TraceDisplayFrameSet retainedSet() {
    return {
        .generation = 1,
        .sequenceNumber = 8,
        .frames = {{
            .frameId = frames::FrameId{8},
            .traceId = display_model::TraceId{1},
            .measurementId = domain::MeasurementId{1},
            .measurementType = domain::MeasurementType::S21,
            .stateRevision = 6,
            .generation = 1,
            .sequenceNumber = 8,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = {1'000'000, 2'000'000},
            .samples = CartesianTraceDisplaySamples{
                TraceDisplayUnit::Decibel, {-1.0, -2.0}},
        }},
    };
}

TEST(ContinuousTraceFailureTest, EmptyPlanObservesWithoutPublishingOrRejecting) {
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), finiteSource()};
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, publicationState(0)};
    ContinuousTracePublisher publisher{acquisition, catalog};
    acquisition.join();
    publisher.join();

    const auto snapshot = publisher.snapshot();
    EXPECT_EQ(snapshot.observedFrames, 1U);
    EXPECT_EQ(snapshot.publishedFrames, 0U);
    EXPECT_EQ(snapshot.rejectedFrames, 0U);
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
}

TEST(ContinuousTraceFailureTest, ProjectionFailurePreservesLastGoodSet) {
    ProjectionFailureSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, publicationState(1)};
    ContinuousTracePublisher publisher{acquisition, catalog};
    SetWait wait{repository};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    const auto retained = wait.finish();
    ASSERT_NE(retained, nullptr);
    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    ASSERT_TRUE(source.waitForRequest(3));
    source.release(3);
    acquisition.join();
    publisher.join();

    const auto snapshot = publisher.snapshot();
    EXPECT_EQ(snapshot.observedFrames, 2U);
    EXPECT_EQ(snapshot.publishedFrames, 1U);
    EXPECT_EQ(snapshot.rejectedFrames, 1U);
    EXPECT_EQ(snapshot.lastObservedSequence, 2U);
    EXPECT_EQ(snapshot.lastPublishedSequence, 1U);
    EXPECT_EQ(repository.latestFrameSet(), retained);
}

TEST(ContinuousTraceFailureTest, CapacityFailurePublishesNoPartialSet) {
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), finiteSource()};
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(repository.publish(retainedSet().frames.front()).hasValue());
    const auto retained = repository.latest(display_model::TraceId{1});
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, publicationState(2)};
    ContinuousTracePublisher publisher{acquisition, catalog};
    acquisition.join();
    publisher.join();

    const auto snapshot = publisher.snapshot();
    EXPECT_EQ(snapshot.observedFrames, 1U);
    EXPECT_EQ(snapshot.publishedFrames, 0U);
    EXPECT_EQ(snapshot.rejectedFrames, 1U);
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
    EXPECT_EQ(repository.latest(display_model::TraceId{1}), retained);
    EXPECT_EQ(repository.latest(display_model::TraceId{2}), nullptr);
}

TEST(ContinuousTraceFailureTest, StaleSetCannotReplaceCurrentPlanResult) {
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, publicationState(1)};
    const auto stalePlan = catalog.capture();
    auto prepared = catalog.prepare(
        publicationState(1, display_model::TraceFormat::Phase), 8);
    ASSERT_TRUE(std::holds_alternative<PreparedTracePublicationPlan>(prepared));
    ASSERT_TRUE(std::holds_alternative<TracePublicationPlanHandle>(
        catalog.commit(std::get<PreparedTracePublicationPlan>(
            std::move(prepared)))));

    const auto stale = catalog.publishIfCurrent(stalePlan, retainedSet());
    ASSERT_TRUE(std::holds_alternative<TracePublicationCatalogError>(stale));
    EXPECT_EQ(
        std::get<TracePublicationCatalogError>(stale).code,
        TracePublicationCatalogErrorCode::StalePublication);
    EXPECT_EQ(repository.latestFrameSet(), nullptr);

    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), finiteSource()};
    ContinuousTracePublisher publisher{acquisition, catalog};
    acquisition.join();
    publisher.join();

    const auto current = repository.latestFrameSet();
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->generation, 2U);
    ASSERT_EQ(current->frames.size(), 1U);
    EXPECT_EQ(current->frames[0].format, display_model::TraceFormat::Phase);
    EXPECT_EQ(publisher.snapshot().publishedFrames, 1U);
}

}  // namespace
}  // namespace vna::application
