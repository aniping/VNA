#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include <vna/application/factory_preset.hpp>
#include <vna/measurement/s_parameter_synthesizer.hpp>
#include <vna/simulation/simulation_sweep.hpp>

namespace vna::simulation {
namespace {

constexpr std::uint64_t kProductSimulationSeed = 0x564E4101ULL;

frames::MeasurementFrame simulatePresetS21(
    const application::FactoryPreset& preset,
    std::uint64_t sequence) {
    const auto& plan = preset.acquisitionPlan;
    const auto instrument = preset.commandBusState.instrument.snapshot();
    const auto measurement = instrument.measurements.front();
    auto payload = simulateOpenPorts({
        .frequencyAxis = plan.frequencyAxis,
        .portCount = plan.portCount,
        .ifBandwidthHz = plan.ifBandwidthHz,
        .powerDbm = plan.powerDbm,
        .seed = kProductSimulationSeed,
        .sequenceNumber = sequence,
    });
    if (!payload.hasValue()) {
        throw std::runtime_error{"factory simulation failed"};
    }
    auto raw = frames::makeRawReceiverFrame(
        {.frameId = frames::FrameId{sequence},
         .sweepId = frames::SweepId{sequence},
         .channelId = preset.acquisitionChannelId,
         .stateRevision = 0,
         .sequenceNumber = sequence},
        plan.frequencyAxis,
        std::move(payload.value()));
    if (!raw.hasValue()) {
        throw std::runtime_error{"factory raw frame failed"};
    }
    auto measured = measurement::synthesizeSParameter(
        std::move(raw.value()), measurement);
    if (!measured.hasValue()) {
        throw std::runtime_error{"factory S21 synthesis failed"};
    }
    return std::move(measured.value());
}

double decibels(const frames::ComplexSample& sample) {
    return 20.0 * std::log10(std::hypot(sample.real, sample.imaginary));
}

double crossSequenceDelta(OpenPortSweepPlan plan) {
    const auto first = simulateOpenPorts(plan);
    ++plan.sequenceNumber;
    const auto second = simulateOpenPorts(plan);
    if (!first.hasValue() || !second.hasValue()) {
        throw std::runtime_error{"noise comparison simulation failed"};
    }
    const auto& left = first.value().sourceStates[0].samples[0].responses[1];
    const auto& right = second.value().sourceStates[0].samples[0].responses[1];
    return std::hypot(left.real - right.real, left.imaginary - right.imaginary);
}

frames::ComplexSample meanCrossResponse(OpenPortSweepPlan plan) {
    constexpr std::uint64_t sampleFrames = 64;
    frames::ComplexSample total{};
    for (std::uint64_t sequence = 0; sequence < sampleFrames; ++sequence) {
        plan.sequenceNumber = sequence;
        const auto result = simulateOpenPorts(plan);
        if (!result.hasValue()) {
            throw std::runtime_error{"noise mean simulation failed"};
        }
        const auto& sample =
            result.value().sourceStates[0].samples[0].responses[1];
        total.real += sample.real;
        total.imaginary += sample.imaginary;
    }
    return {total.real / static_cast<double>(sampleFrames),
            total.imaginary / static_cast<double>(sampleFrames)};
}

double distance(
    const frames::ComplexSample& left,
    const frames::ComplexSample& right) {
    return std::hypot(left.real - right.real, left.imaginary - right.imaginary);
}

TEST(SimulationVisibilityTest, FactoryPresetS21IsVisibleAndFrequencyDependent) {
    const auto preset = application::makeFactoryPreset();
    const auto measured = simulatePresetS21(preset, 1);
    std::vector<double> values;
    values.reserve(measured.samples.size());
    std::transform(
        measured.samples.cbegin(), measured.samples.cend(),
        std::back_inserter(values), decibels);

    const auto [minimum, maximum] =
        std::minmax_element(values.cbegin(), values.cend());
    ASSERT_EQ(values.size(), 201U);
    EXPECT_GT(*minimum, -80.0);
    EXPECT_LT(*maximum, -40.0);
    EXPECT_GT(*maximum - *minimum, 2.0);
}

TEST(SimulationVisibilityTest, AdjacentFramesKeepTrendButRetainNoiseMotion) {
    const auto preset = application::makeFactoryPreset();
    const auto first = simulatePresetS21(preset, 10);
    const auto second = simulatePresetS21(preset, 11);
    double maximumDbMotion = 0.0;
    bool rawSampleChanged = false;
    for (std::size_t index = 0; index < first.samples.size(); ++index) {
        rawSampleChanged = rawSampleChanged ||
                           first.samples[index] != second.samples[index];
        maximumDbMotion = std::max(
            maximumDbMotion,
            std::abs(decibels(first.samples[index]) -
                     decibels(second.samples[index])));
    }

    EXPECT_TRUE(rawSampleChanged);
    EXPECT_LT(maximumDbMotion, 0.75);
}

TEST(SimulationVisibilityTest, IfBandwidthAndPowerChangeOnlyNoiseMotion) {
    const auto preset = application::makeFactoryPreset();
    const auto& acquisition = preset.acquisitionPlan;
    OpenPortSweepPlan quiet{
        .frequencyAxis = acquisition.frequencyAxis,
        .portCount = acquisition.portCount,
        .ifBandwidthHz = 100,
        .powerDbm = 0.0,
        .seed = kProductSimulationSeed,
        .sequenceNumber = 20,
    };
    auto wide = quiet;
    wide.ifBandwidthHz = 10'000;
    auto weak = quiet;
    weak.powerDbm = -20.0;

    const auto quietMotion = crossSequenceDelta(quiet);
    const auto wideMotion = crossSequenceDelta(wide);
    const auto weakMotion = crossSequenceDelta(weak);
    const auto quietMean = meanCrossResponse(quiet);
    const auto wideMean = meanCrossResponse(wide);
    const auto weakMean = meanCrossResponse(weak);
    EXPECT_GT(wideMotion, quietMotion);
    EXPECT_GT(weakMotion, quietMotion);
    EXPECT_DOUBLE_EQ(wideMotion, weakMotion);
    EXPECT_LT(distance(quietMean, wideMean), 5.0e-6);
    EXPECT_LT(distance(quietMean, weakMean), 5.0e-6);
}

}  // namespace
}  // namespace vna::simulation
