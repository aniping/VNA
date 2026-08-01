#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include <vna/simulation/simulation_sweep.hpp>

namespace vna::simulation {
namespace {

frames::FrequencyAxis validAxis(std::uint32_t points = 5) {
    return frames::FrequencyAxis{
        .id = frames::FrequencyAxisId{1},
        .startFrequencyHz = 1'000'000,
        .stopFrequencyHz = 5'000'000,
        .points = points,
    };
}

TEST(SimulationSweepTest, GeneratesKnownFivePointReceiverSamples) {
    const std::array<frames::ComplexSample, 5> expectedB1{{
        {0.5, 0.0},
        {0.25, 0.1875},
        {0.0, 0.25},
        {-0.25, 0.1875},
        {-0.5, 0.0},
    }};

    const auto result = simulateSweep(validAxis());

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().sourceStates.size(), 2U);
    const auto& firstSource = result.value().sourceStates[0];
    const auto& secondSource = result.value().sourceStates[1];
    ASSERT_EQ(firstSource.samples.size(), expectedB1.size());
    for (std::size_t index = 0; index < expectedB1.size(); ++index) {
        const auto& first = firstSource.samples[index];
        const auto& second = secondSource.samples[index];
        EXPECT_DOUBLE_EQ(first.reference.real, 1.0);
        EXPECT_DOUBLE_EQ(first.reference.imaginary, 0.0);
        EXPECT_DOUBLE_EQ(first.responses[0].real, expectedB1[index].real);
        EXPECT_DOUBLE_EQ(first.responses[0].imaginary, expectedB1[index].imaginary);
        EXPECT_DOUBLE_EQ(second.responses[1].real, expectedB1[index].real);
        EXPECT_DOUBLE_EQ(
            second.responses[1].imaginary, expectedB1[index].imaginary);
    }
}

TEST(SimulationSweepTest, RepeatsIdenticalInputExactly) {
    const auto first = simulateSweep(validAxis(17));
    const auto second = simulateSweep(validAxis(17));

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(first.value(), second.value());
}

TEST(SimulationSweepTest, ProducesFiniteBoundedMaximumSweep) {
    const auto result = simulateSweep(validAxis(frames::kMaxSweepPoints));

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().sourceStates.size(), 2U);
    for (const auto& state : result.value().sourceStates) {
        ASSERT_EQ(state.samples.size(), frames::kMaxSweepPoints);
        for (const auto& sample : state.samples) {
            for (const auto& response : sample.responses) {
                EXPECT_TRUE(std::isfinite(response.real));
                EXPECT_TRUE(std::isfinite(response.imaginary));
                EXPECT_LE(std::hypot(response.real, response.imaginary), 0.5);
            }
        }
    }
}

struct InvalidAxisCase {
    const char* name;
    frames::FrequencyAxis axis;
    frames::FrameErrorCode expected;
};

class InvalidSimulationAxisTest
    : public ::testing::TestWithParam<InvalidAxisCase> {};

TEST_P(InvalidSimulationAxisTest, RejectsInvalidAxisBeforeAllocatingSamples) {
    const auto result = simulateSweep(GetParam().axis);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, GetParam().expected);
}

std::array<InvalidAxisCase, 4> invalidAxes() {
    const auto validStart = 1'000'000ULL;
    const auto validStop = 2'000'000ULL;
    return {{
        {"MissingId", {frames::FrequencyAxisId{0}, validStart, validStop, 2},
         frames::FrameErrorCode::InvalidFrequencyAxis},
        {"Reversed", {frames::FrequencyAxisId{1}, validStop, validStart, 2},
         frames::FrameErrorCode::InvalidFrequencyAxis},
        {"TooShort", {frames::FrequencyAxisId{1}, validStart, validStop, 1},
         frames::FrameErrorCode::InvalidFrequencyAxis},
        {"TooLong",
         {frames::FrequencyAxisId{1}, validStart, validStop,
          frames::kMaxSweepPoints + 1},
         frames::FrameErrorCode::PointCountExceeded},
    }};
}

INSTANTIATE_TEST_SUITE_P(
    InvalidBoundary,
    InvalidSimulationAxisTest,
    ::testing::ValuesIn(invalidAxes()),
    [](const auto& info) { return info.param.name; });

}  // namespace
}  // namespace vna::simulation
