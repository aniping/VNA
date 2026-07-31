#include <gtest/gtest.h>

#include <array>

#include <vna/data_plane/log_magnitude_projector.hpp>

namespace vna::data_plane {
namespace {

frames::MeasurementFrame measurementFrame(
    std::vector<frames::ComplexSample> samples) {
    return frames::MeasurementFrame{
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
        .type = domain::MeasurementType::S11,
        .samples = std::move(samples),
    };
}

TEST(LogMagnitudeProjectorTest, ProjectsKnownS11ValuesToDecibels) {
    const auto source = measurementFrame({
        {0.5, 0.0},
        {0.25, 0.1875},
        {0.0, 0.25},
        {-0.25, 0.1875},
        {-0.5, 0.0},
    });
    const std::array<double, 5> expected{{
        -6.020599913279624,
        -10.10299956639812,
        -12.041199826559248,
        -10.10299956639812,
        -6.020599913279624,
    }};

    const auto result = projectLogMagnitude(source);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_NEAR(result.value()[index], expected[index], 1e-12);
    }
}

TEST(LogMagnitudeProjectorTest, RejectsZeroMagnitudeWithoutPartialValues) {
    const auto source = measurementFrame({
        {0.5, 0.0},
        {0.0, 0.0},
        {-0.5, 0.0},
    });

    const auto result = projectLogMagnitude(source);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(
        result.error().code,
        frames::FrameErrorCode::NonFiniteTraceValue);
}

}  // namespace
}  // namespace vna::data_plane
