#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stop_token>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {

StateSnapshot publicationState(
    display_model::TraceFormat format,
    std::uint64_t revision) {
    StateSnapshot state{
        .stateRevision = revision,
        .control = {},
        .instrument = {
            .channels = {{
                .id = domain::ChannelId{1},
                .sweep = {
                    .startFrequencyHz = 1'000'000,
                    .stopFrequencyHz = 2'000'000,
                    .points = 3,
                    .ifBandwidthHz = 10'000,
                    .powerDbm = -10.5,
                },
            }}},
        .display = {},
    };
    state.instrument.measurements = {{
        domain::MeasurementId{1}, domain::ChannelId{1},
        domain::MeasurementType::S21}};
    state.display.traces = {{
        display_model::TraceId{1}, display_model::WindowId{1},
        domain::MeasurementId{1}, format, std::nullopt}};
    return state;
}

auto finiteSource(acquisition::test_support::ControlledSource controlled) {
    return [controlled](const acquisition::ContinuousAcquisitionPlan& plan,
                        std::uint64_t sequence,
                        std::stop_token token) {
        if (sequence == 1) {
            return controlled(plan, sequence, token);
        }
        return frames::Result<frames::RawReceiverPayload>{
            frames::FrameError{frames::FrameErrorCode::InvalidPortCount}};
    };
}

TEST(ContinuousTracePlanTest, CapturesCurrentPlanAfterRawArrives) {
    acquisition::test_support::ControlledSource controlled;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), finiteSource(controlled)};
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository,
        publicationState(display_model::TraceFormat::LogMagnitude, 7)};
    ContinuousTracePublisher publisher{acquisition, catalog};
    ASSERT_TRUE(controlled.waitForRequest(1));

    auto prepared = catalog.prepare(
        publicationState(display_model::TraceFormat::Phase, 8), 8);
    ASSERT_TRUE(std::holds_alternative<PreparedTracePublicationPlan>(prepared));
    auto committed = catalog.commit(
        std::get<PreparedTracePublicationPlan>(std::move(prepared)));
    ASSERT_TRUE(std::holds_alternative<TracePublicationPlanHandle>(committed));
    controlled.release(1);
    acquisition.join();
    publisher.join();

    const auto set = repository.latestFrameSet();
    ASSERT_NE(set, nullptr);
    ASSERT_EQ(set->frames.size(), 1U);
    EXPECT_EQ(set->generation, 2U);
    EXPECT_EQ(set->frames[0].stateRevision, 8U);
    EXPECT_EQ(set->frames[0].format, display_model::TraceFormat::Phase);
    const auto snapshot = publisher.snapshot();
    EXPECT_EQ(snapshot.observedFrames, 1U);
    EXPECT_EQ(snapshot.publishedFrames, 1U);
    EXPECT_EQ(snapshot.rejectedFrames, 0U);
}

}  // namespace
}  // namespace vna::application
