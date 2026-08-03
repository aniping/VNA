#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <set>
#include <utility>

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

OpenPortSweepPlan validPlan(std::uint32_t ports = 2) {
    return OpenPortSweepPlan{
        .frequencyAxis = validAxis(),
        .portCount = ports,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
        .seed = 0x1234U,
        .sequenceNumber = 7,
    };
}

double magnitude(const frames::ComplexSample& value) {
    return std::hypot(value.real, value.imaginary);
}

TEST(SimulationSweepTest, LegacyEntryPreservesCompleteTwoPortShape) {
    const std::array<frames::ComplexSample, 5> expected{{
        {0.5, 0.0},
        {0.25, 0.1875},
        {0.0, 0.25},
        {-0.25, 0.1875},
        {-0.5, 0.0},
    }};
    const auto result = simulateSweep(validAxis());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().portCount, 2U);
    ASSERT_EQ(result.value().sourceStates.size(), 2U);
    for (std::size_t source = 0; source < 2; ++source) {
        const auto& state = result.value().sourceStates[source];
        EXPECT_EQ(state.sourcePort, source + 1);
        ASSERT_EQ(state.samples.size(), expected.size());
        for (std::size_t point = 0; point < expected.size(); ++point) {
            EXPECT_EQ(state.samples[point].reference,
                      (frames::ComplexSample{1.0, 0.0}));
            ASSERT_EQ(state.samples[point].responses.size(), 2U);
            EXPECT_EQ(state.samples[point].responses[source], expected[point]);
            EXPECT_EQ(state.samples[point].responses[1 - source],
                      (frames::ComplexSample{0.0, 0.0}));
        }
    }
}

TEST(SimulationSweepTest, RepeatsIdenticalInputExactly) {
    const auto first = simulateOpenPorts(validPlan());
    const auto second = simulateOpenPorts(validPlan());

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(first.value(), second.value());
}

TEST(SimulationSweepTest, ModelsEverySourceAndResponseAsOpenPorts) {
    auto plan = validPlan(4);
    plan.frequencyAxis = validAxis(17);

    const auto result = simulateOpenPorts(plan);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().sourceStates.size(), 4U);
    std::set<std::uint32_t> sources;
    for (const auto& state : result.value().sourceStates) {
        sources.insert(state.sourcePort);
        ASSERT_EQ(state.samples.size(), 17U);
        for (const auto& sample : state.samples) {
            ASSERT_EQ(sample.responses.size(), 4U);
            const auto own = magnitude(
                sample.responses.at(state.sourcePort - 1));
            EXPECT_GT(own / magnitude(sample.reference), 0.98);
            EXPECT_LT(own / magnitude(sample.reference), 1.02);
            for (std::uint32_t response = 1; response <= 4; ++response) {
                if (response != state.sourcePort) {
                    EXPECT_LT(magnitude(sample.responses[response - 1]), 0.01);
                }
            }
        }
    }
    EXPECT_EQ(sources, (std::set<std::uint32_t>{1, 2, 3, 4}));
}

TEST(SimulationSweepTest, NoiseVariesBySequenceFrequencyAndPort) {
    auto next = validPlan();
    ++next.sequenceNumber;

    const auto first = simulateOpenPorts(validPlan());
    const auto second = simulateOpenPorts(next);

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_NE(first.value(), second.value());
    const auto& payload = first.value();
    EXPECT_NE(
        payload.sourceStates[0].samples[0].responses[1],
        payload.sourceStates[0].samples[1].responses[1]);
    EXPECT_NE(
        payload.sourceStates[0].samples[0].responses[1],
        payload.sourceStates[1].samples[0].responses[0]);
}

TEST(SimulationSweepTest, CoordinateFieldsCannotCancelEachOther) {
    auto plan = validPlan(4);
    plan.frequencyAxis = validAxis(513);
    auto swapped = plan;
    std::swap(swapped.seed, swapped.sequenceNumber);

    const auto result = simulateOpenPorts(plan);
    const auto swappedResult = simulateOpenPorts(swapped);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(swappedResult.hasValue());
    EXPECT_NE(result.value(), swappedResult.value());
    EXPECT_NE(result.value().sourceStates[0].samples[256].responses[2],
              result.value().sourceStates[1].samples[512].responses[2]);
}

TEST(SimulationSweepTest, LocksKnownDeterministicRawPoint) {
    const auto result = simulateOpenPorts(validPlan());

    ASSERT_TRUE(result.hasValue());
    const auto& point = result.value().sourceStates[0].samples[0];
    EXPECT_DOUBLE_EQ(point.reference.real, 1.0000073820094573);
    EXPECT_DOUBLE_EQ(point.reference.imaginary, 9.1762821777476308e-6);
    EXPECT_DOUBLE_EQ(point.responses[0].real, 1.0000762583020506);
    EXPECT_DOUBLE_EQ(point.responses[0].imaginary, 9.4904936105068025e-5);
    EXPECT_DOUBLE_EQ(point.responses[1].real, 0.00099651377022354952);
    EXPECT_DOUBLE_EQ(point.responses[1].imaginary, 0.001389736342026225);
}

TEST(SimulationSweepTest, ProducesFiniteBoundedMaximumSweep) {
    auto plan = validPlan(4);
    plan.frequencyAxis = validAxis(frames::kMaxSweepPoints);
    const auto result = simulateOpenPorts(plan);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().sourceStates.size(), 4U);
    for (const auto& state : result.value().sourceStates) {
        ASSERT_EQ(state.samples.size(), frames::kMaxSweepPoints);
        for (const auto& sample : state.samples) {
            for (const auto& response : sample.responses) {
                EXPECT_TRUE(std::isfinite(response.real));
                EXPECT_TRUE(std::isfinite(response.imaginary));
                EXPECT_LE(std::hypot(response.real, response.imaginary), 1.01);
            }
        }
    }
}

TEST(SimulationSweepTest, RejectsOutOfRangePortCountAndNoiseInputs) {
    auto badPorts = validPlan(frames::kMaxPortCount + 1);
    auto noBandwidth = validPlan();
    noBandwidth.ifBandwidthHz = 0;

    const auto portsResult = simulateOpenPorts(badPorts);
    const auto bandwidthResult = simulateOpenPorts(noBandwidth);

    ASSERT_FALSE(portsResult.hasValue());
    EXPECT_EQ(portsResult.error().code, frames::FrameErrorCode::InvalidPortCount);
    ASSERT_FALSE(bandwidthResult.hasValue());
    EXPECT_EQ(
        bandwidthResult.error().code,
        frames::FrameErrorCode::InvalidAcquisitionSettings);
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
