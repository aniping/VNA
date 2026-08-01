#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include <vna/application/command_bus.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application {
namespace {

domain::ChannelSnapshot channel(std::uint64_t id) {
    return {
        .id = domain::ChannelId{id},
        .sweep = {1, 2, 2, 1, 0.0},
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
        display_model::TraceId{id},
        display_model::WindowId{1},
        domain::MeasurementId{measurementId},
        format,
        std::nullopt,
    };
}

StateSnapshot state() {
    return {
        .stateRevision = 7,
        .control = {},
        .instrument = {
            .channels = {channel(1)},
            .measurements = {measurement(1, 1)},
        },
        .display = {.windows = {}, .traces = {trace(1, 1)}},
    };
}

TraceDisplayFrameSet frameSet(
    std::uint64_t generation,
    std::uint64_t sequence = 1) {
    return {
        generation,
        sequence,
        {{
            frames::FrameId{sequence},
            display_model::TraceId{1},
            domain::MeasurementId{1},
            domain::MeasurementType::S21,
            7,
            generation,
            sequence,
            display_model::TraceFormat::LogMagnitude,
            {1.0, 2.0},
            CartesianTraceDisplaySamples{
                TraceDisplayUnit::Decibel, {-1.0, -2.0}},
        }},
    };
}

TracePublicationPlanHandle commitPrepared(
    TracePublicationCatalog& catalog,
    TracePublicationPrepareResult prepared) {
    auto committed = catalog.commit(
        std::get<PreparedTracePublicationPlan>(std::move(prepared)));
    return std::get<TracePublicationPlanHandle>(std::move(committed));
}

TEST(TracePublicationCatalogValidationTest, OrdersTargetsAndIgnoresOtherChannel) {
    auto candidate = state();
    candidate.instrument.channels.push_back(channel(2));
    candidate.instrument.measurements.push_back(measurement(
        2, 2, static_cast<domain::MeasurementType>(99)));
    candidate.display.traces = {trace(3, 1), trace(2, 2), trace(1, 1)};
    TraceDisplayFrameRepository repository{4};

    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, candidate};
    const auto plan = catalog.capture();

    ASSERT_EQ(plan->targets.size(), 2U);
    EXPECT_EQ(plan->targets[0].trace.id, display_model::TraceId{1});
    EXPECT_EQ(plan->targets[1].trace.id, display_model::TraceId{3});
    EXPECT_EQ(plan->targets[0].measurement.id, domain::MeasurementId{1});
    EXPECT_EQ(plan->targets[0].measurement.channelId, domain::ChannelId{1});
    EXPECT_EQ(plan->targets[0].measurement.type, domain::MeasurementType::S21);
    EXPECT_EQ(
        plan->targets[0].trace.measurementId, domain::MeasurementId{1});
    EXPECT_EQ(
        plan->targets[0].trace.format,
        display_model::TraceFormat::LogMagnitude);
}

TEST(TracePublicationCatalogValidationTest, NonMaterialChangesKeepGeneration) {
    auto initial = state();
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{domain::ChannelId{1}, repository, initial};
    const auto oldPlan = catalog.capture();
    const auto retained = std::get<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(frameSet(1)));
    auto candidate = initial;
    candidate.display.traces[0].windowId = display_model::WindowId{9};
    candidate.display.traces[0].scale = display_model::CartesianScaleSnapshot{
        5.0, 0.0, 8.0, -40.0, 10.0, display_model::ScaleUnit::Decibel};

    const auto updated = commitPrepared(catalog, catalog.prepare(candidate, 44));

    EXPECT_EQ(updated->generation, 1U);
    EXPECT_EQ(updated->stateRevision, 44U);
    EXPECT_EQ(repository.latestFrameSet(), retained);
    EXPECT_TRUE(std::holds_alternative<TraceDisplayFrameSetHandle>(
        catalog.publishIfCurrent(oldPlan, frameSet(1, 2))));
}

TEST(TracePublicationCatalogValidationTest, MaterialIdentityUsesAllKeys) {
    auto candidate = state();
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{domain::ChannelId{1}, repository, candidate};
    candidate.instrument.measurements[0].type = domain::MeasurementType::S11;
    const auto typeChanged =
        commitPrepared(catalog, catalog.prepare(candidate, 8));
    EXPECT_EQ(typeChanged->generation, 2U);

    candidate.instrument.measurements.push_back(
        measurement(2, 1, domain::MeasurementType::S11));
    candidate.display.traces[0].measurementId = domain::MeasurementId{2};
    const auto measurementChanged =
        commitPrepared(catalog, catalog.prepare(candidate, 9));
    EXPECT_EQ(measurementChanged->generation, 3U);

    candidate.display.traces.push_back(trace(2, 2));
    const auto targetSetChanged =
        commitPrepared(catalog, catalog.prepare(candidate, 10));
    EXPECT_EQ(targetSetChanged->generation, 4U);
}

TEST(TracePublicationCatalogValidationTest, CompilationFailuresAreAtomic) {
    const auto initial = state();
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{domain::ChannelId{1}, repository, initial};
    const auto originalPlan = catalog.capture();
    const auto originalSet = std::get<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(frameSet(1)));
    auto missingMeasurement = initial;
    missingMeasurement.display.traces[0].measurementId =
        domain::MeasurementId{99};
    auto missingChannel = initial;
    missingChannel.instrument.measurements[0].channelId = domain::ChannelId{99};
    auto duplicateTrace = initial;
    duplicateTrace.display.traces.push_back(duplicateTrace.display.traces[0]);
    auto unsupportedMeasurement = initial;
    unsupportedMeasurement.instrument.measurements[0].type =
        static_cast<domain::MeasurementType>(99);
    auto unsupportedFormat = initial;
    unsupportedFormat.display.traces[0].format =
        static_cast<display_model::TraceFormat>(99);
    const std::vector cases{
        std::pair{missingMeasurement,
                  TracePublicationCatalogErrorCode::MeasurementNotFound},
        std::pair{missingChannel,
                  TracePublicationCatalogErrorCode::ChannelNotFound},
        std::pair{duplicateTrace,
                  TracePublicationCatalogErrorCode::DuplicateTraceId},
        std::pair{unsupportedMeasurement,
                  TracePublicationCatalogErrorCode::UnsupportedMeasurementType},
        std::pair{unsupportedFormat,
                  TracePublicationCatalogErrorCode::UnsupportedTraceFormat},
    };

    for (const auto& [candidate, expected] : cases) {
        const auto result = catalog.prepare(candidate, 8);
        ASSERT_TRUE(
            std::holds_alternative<TracePublicationCatalogError>(result));
        EXPECT_EQ(std::get<TracePublicationCatalogError>(result).code, expected);
        EXPECT_EQ(catalog.capture(), originalPlan);
        EXPECT_EQ(repository.latestFrameSet(), originalSet);
    }
}

TEST(TracePublicationCatalogValidationTest, RejectsStalePublication) {
    auto initial = state();
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{domain::ChannelId{1}, repository, initial};
    const auto oldPlan = catalog.capture();
    initial.display.traces[0].format = display_model::TraceFormat::Phase;
    const auto current = commitPrepared(catalog, catalog.prepare(initial, 8));

    const auto stale = catalog.publishIfCurrent(oldPlan, frameSet(1));

    ASSERT_TRUE(std::holds_alternative<TracePublicationCatalogError>(stale));
    EXPECT_EQ(
        std::get<TracePublicationCatalogError>(stale).code,
        TracePublicationCatalogErrorCode::StalePublication);
    EXPECT_EQ(current->generation, 2U);
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
}

TEST(TracePublicationCatalogValidationTest, RepositoryFailureKeepsLastSet) {
    TraceDisplayFrameRepository repository{4};
    TracePublicationCatalog catalog{domain::ChannelId{1}, repository, state()};
    const auto plan = catalog.capture();
    const auto retained = std::get<TraceDisplayFrameSetHandle>(
        repository.publishFrameSet(frameSet(1)));

    const auto rejected = catalog.publishIfCurrent(
        plan, TraceDisplayFrameSet{1, 2, {}});

    ASSERT_TRUE(std::holds_alternative<TracePublicationCatalogError>(rejected));
    const auto& error = std::get<TracePublicationCatalogError>(rejected);
    EXPECT_EQ(error.code, TracePublicationCatalogErrorCode::RepositoryRejected);
    ASSERT_TRUE(error.repositoryError.has_value());
    EXPECT_EQ(
        error.repositoryError->code,
        TraceDisplayFrameSetErrorCode::EmptyFrameSet);
    EXPECT_EQ(repository.latestFrameSet(), retained);
}

}  // namespace
}  // namespace vna::application
