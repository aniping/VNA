#include <gtest/gtest.h>

#include <vna/data_plane/trace_projector.hpp>

#include <array>
#include <limits>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace vna::data_plane {
namespace {

frames::MeasurementFrame measurementFrame(
    domain::MeasurementType type,
    std::vector<frames::ComplexSample> samples) {
    return {
        .context = {
            .frameId = frames::FrameId{11},
            .sweepId = frames::SweepId{7},
            .channelId = domain::ChannelId{3},
            .stateRevision = 19,
            .sequenceNumber = 5,
        },
        .frequencyAxis = {
            .id = frames::FrequencyAxisId{13},
            .startFrequencyHz = 1'000'000,
            .stopFrequencyHz = 5'000'000,
            .points = static_cast<std::uint32_t>(samples.size()),
        },
        .measurementId = domain::MeasurementId{17},
        .type = type,
        .samples = std::move(samples),
    };
}

TEST(TraceProjectorTest, LogMagnitudeProducesDecibelScalars) {
    const auto source = measurementFrame(
        domain::MeasurementType::S12,
        {{1.0, 0.0}, {0.1, 0.0}});

    const auto result = projectTraceSamples(
        source,
        display_model::TraceFormat::LogMagnitude);

    ASSERT_TRUE(result.hasValue());
    const auto* scalar = std::get_if<ScalarTraceSamples>(&result.value());
    ASSERT_NE(scalar, nullptr);
    EXPECT_EQ(scalar->unit, ProjectedTraceUnit::Decibel);
    ASSERT_EQ(scalar->values.size(), 2U);
    EXPECT_DOUBLE_EQ(scalar->values[0], 0.0);
    EXPECT_NEAR(scalar->values[1], -20.0, 1e-12);
}

TEST(TraceProjectorTest, PhaseUsesWrappedDegreesAndDefinesComplexZero) {
    const auto source = measurementFrame(
        domain::MeasurementType::S21,
        {{1.0, 0.0}, {0.0, 1.0}, {-1.0, 0.0}, {0.0, -1.0}, {0.0, 0.0}});

    const auto result =
        projectTraceSamples(source, display_model::TraceFormat::Phase);

    ASSERT_TRUE(result.hasValue());
    const auto* scalar = std::get_if<ScalarTraceSamples>(&result.value());
    ASSERT_NE(scalar, nullptr);
    EXPECT_EQ(scalar->unit, ProjectedTraceUnit::Degree);
    EXPECT_EQ(scalar->values,
              (std::vector<double>{0.0, 90.0, -180.0, -90.0, 0.0}));
}

TEST(TraceProjectorTest, SmithPreservesComplexSamplesForEverySParameter) {
    const std::array types{
        domain::MeasurementType::S11,
        domain::MeasurementType::S21,
        domain::MeasurementType::S12,
        domain::MeasurementType::S22,
    };
    const std::vector<frames::ComplexSample> expected{
        {0.5, -0.25},
        {-1.0, 1.0},
    };

    for (const auto type : types) {
        const auto result = projectTraceSamples(
            measurementFrame(type, expected),
            display_model::TraceFormat::Smith);

        ASSERT_TRUE(result.hasValue());
        const auto* complex =
            std::get_if<ComplexTraceSamples>(&result.value());
        ASSERT_NE(complex, nullptr);
        EXPECT_EQ(complex->unit, ProjectedTraceUnit::Unitless);
        EXPECT_EQ(complex->values, expected);
    }
}

TEST(TraceProjectorTest, RejectsIncompleteMeasurementFrameBeforeProjection) {
    auto source = measurementFrame(
        domain::MeasurementType::S21,
        {{1.0, 0.0}, {0.5, 0.0}});
    source.frequencyAxis.points = 3;

    const auto result = projectTraceSamples(
        source,
        display_model::TraceFormat::LogMagnitude);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::SampleCountMismatch);
}

TEST(TraceSampleRangeProjectorTest, ProjectsLogMagnitudeWithoutACompleteFrame) {
    const std::array samples{
        frames::ComplexSample{1.0, 0.0},
        frames::ComplexSample{0.1, 0.0},
    };

    const auto result = projectTraceSamples(
        std::span<const frames::ComplexSample>{samples},
        display_model::TraceFormat::LogMagnitude);

    ASSERT_TRUE(result.hasValue());
    const auto* scalar = std::get_if<ScalarTraceSamples>(&result.value());
    ASSERT_NE(scalar, nullptr);
    EXPECT_EQ(scalar->unit, ProjectedTraceUnit::Decibel);
    ASSERT_EQ(scalar->values.size(), 2U);
    EXPECT_DOUBLE_EQ(scalar->values[0], 0.0);
    EXPECT_NEAR(scalar->values[1], -20.0, 1e-12);
}

TEST(TraceSampleRangeProjectorTest, RejectsNonFiniteComplexInput) {
    const std::array samples{
        frames::ComplexSample{
            std::numeric_limits<double>::quiet_NaN(), 0.0},
    };

    const auto result = projectTraceSamples(
        std::span<const frames::ComplexSample>{samples},
        display_model::TraceFormat::Phase);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::NonFiniteSample);
}

TEST(TraceProjectorTest, RejectsUnknownTraceFormat) {
    const auto source = measurementFrame(
        domain::MeasurementType::S11,
        {{1.0, 0.0}, {0.5, 0.0}});

    const auto result = projectTraceSamples(
        source,
        static_cast<display_model::TraceFormat>(99));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(
        result.error().code,
        frames::FrameErrorCode::UnsupportedTraceFormat);
}

}  // namespace
}  // namespace vna::data_plane
