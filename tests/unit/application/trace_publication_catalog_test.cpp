#include <gtest/gtest.h>

#include <vna/application/command_bus.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application {
namespace {

domain::ChannelSnapshot channel(std::uint64_t id) {
    return {
        .id = domain::ChannelId{id},
        .sweep = {
            .startFrequencyHz = 1,
            .stopFrequencyHz = 2,
            .points = 2,
            .ifBandwidthHz = 1,
            .powerDbm = 0.0,
        },
    };
}

StateSnapshot emptyState(std::uint64_t revision = 7) {
    return {
        .stateRevision = revision,
        .control = {},
        .instrument = {.channels = {channel(1)}, .measurements = {}},
        .display = {},
    };
}

domain::MeasurementSnapshot measurement(
    std::uint64_t id,
    std::uint64_t channelId,
    domain::MeasurementType type = domain::MeasurementType::S21) {
    return {domain::MeasurementId{id}, domain::ChannelId{channelId}, type};
}

display_model::TraceSnapshot trace(
    std::uint64_t id,
    std::uint64_t measurementId,
    display_model::TraceFormat format =
        display_model::TraceFormat::LogMagnitude) {
    return {
        .id = display_model::TraceId{id},
        .windowId = display_model::WindowId{1},
        .measurementId = domain::MeasurementId{measurementId},
        .format = format,
        .scale = std::nullopt,
    };
}

TraceDisplayFrameSet frameSet(
    std::uint64_t generation,
    std::uint64_t sequenceNumber = 1) {
    return {
        .generation = generation,
        .sequenceNumber = sequenceNumber,
        .frames = {{
            .frameId = frames::FrameId{sequenceNumber},
            .traceId = display_model::TraceId{1},
            .measurementId = domain::MeasurementId{1},
            .measurementType = domain::MeasurementType::S21,
            .stateRevision = 7,
            .generation = generation,
            .sequenceNumber = sequenceNumber,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = {1.0, 2.0},
            .samples = CartesianTraceDisplaySamples{
                TraceDisplayUnit::Decibel, {-1.0, -2.0}},
        }},
    };
}

StateSnapshot singleTraceState(
    display_model::TraceFormat format =
        display_model::TraceFormat::LogMagnitude) {
    auto state = emptyState();
    state.instrument.measurements = {measurement(1, 1)};
    state.display.traces = {trace(1, 1, format)};
    return state;
}

TEST(TracePublicationCatalogTest, StartsWithImmutableEmptyPlan) {
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, emptyState()};

    const auto plan = catalog.capture();

    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->generation, 1U);
    EXPECT_EQ(plan->stateRevision, 7U);
    EXPECT_EQ(plan->channelId, domain::ChannelId{1});
    EXPECT_TRUE(plan->targets.empty());
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
}

TEST(TracePublicationCatalogTest, MaterialChangeAdvancesAndDiscardsAtomically) {
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, singleTraceState()};
    ASSERT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(frameSet(1))));
    auto changed = singleTraceState(display_model::TraceFormat::Phase);

    auto prepared = catalog.prepare(changed, 8);
    ASSERT_TRUE(std::holds_alternative<PreparedTracePublicationPlan>(prepared));
    auto committed = catalog.commit(
        std::get<PreparedTracePublicationPlan>(std::move(prepared)));

    ASSERT_TRUE(std::holds_alternative<TracePublicationPlanHandle>(committed));
    const auto plan = std::get<TracePublicationPlanHandle>(committed);
    EXPECT_EQ(plan->generation, 2U);
    EXPECT_EQ(plan->stateRevision, 8U);
    ASSERT_EQ(plan->targets.size(), 1U);
    EXPECT_EQ(plan->targets[0].trace.format, display_model::TraceFormat::Phase);
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
    EXPECT_EQ(catalog.capture(), plan);
}

TEST(TracePublicationCatalogTest, RejectsOlderPreparedPlanWithinGeneration) {
    TraceDisplayFrameRepository repository{4};
    auto initial = singleTraceState();
    TracePublicationCatalog catalog{domain::ChannelId{1}, repository, initial};
    auto olderState = initial;
    olderState.display.traces[0].windowId = display_model::WindowId{2};
    auto newerState = initial;
    newerState.display.traces[0].windowId = display_model::WindowId{3};
    auto older = catalog.prepare(olderState, 8);
    auto newer = catalog.prepare(newerState, 9);

    ASSERT_TRUE(std::holds_alternative<TracePublicationPlanHandle>(
        catalog.commit(std::get<PreparedTracePublicationPlan>(
            std::move(newer)))));
    const auto stale = catalog.commit(
        std::get<PreparedTracePublicationPlan>(std::move(older)));

    ASSERT_TRUE(std::holds_alternative<TracePublicationCatalogError>(stale));
    EXPECT_EQ(
        std::get<TracePublicationCatalogError>(stale).code,
        TracePublicationCatalogErrorCode::StalePrepared);
    EXPECT_EQ(catalog.capture()->stateRevision, 9U);
    EXPECT_EQ(
        catalog.capture()->targets[0].trace.windowId,
        display_model::WindowId{3});
}

}  // namespace
}  // namespace vna::application
