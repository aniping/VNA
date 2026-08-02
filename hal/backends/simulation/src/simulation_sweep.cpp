#include <vna/simulation/simulation_sweep.hpp>

#include "open_port_leakage.hpp"
#include "open_port_range.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace vna::simulation {
namespace {

std::optional<frames::FrameError> validatePlan(const OpenPortSweepPlan& plan) {
    const auto& axis = plan.frequencyAxis;
    // Check the capacity boundary before allocating so malformed requests can
    // never force the simulation backend to reserve an unbounded vector.
    if (axis.points > frames::kMaxSweepPoints) {
        return frames::FrameError{
            .code = frames::FrameErrorCode::PointCountExceeded};
    }
    if (axis.id.value() == 0 ||
        axis.startFrequencyHz >= axis.stopFrequencyHz || axis.points < 2) {
        return frames::FrameError{
            .code = frames::FrameErrorCode::InvalidFrequencyAxis};
    }
    if (plan.portCount == 0 || plan.portCount > frames::kMaxPortCount) {
        return frames::FrameError{
            .code = frames::FrameErrorCode::InvalidPortCount};
    }
    if (plan.ifBandwidthHz == 0 || !std::isfinite(plan.powerDbm)) {
        return frames::FrameError{
            .code = frames::FrameErrorCode::InvalidAcquisitionSettings};
    }
    return std::nullopt;
}

struct ReceiverCoordinate {
    std::uint32_t point;
    std::uint32_t sourcePort;
    std::uint32_t responsePort;
};

std::uint64_t mix(std::uint64_t value) {
    // SplitMix64's fixed unsigned operations are specified modulo 2^64. They
    // give the same coordinate bits on both supported toolchains without a
    // standard-library random distribution whose mapping may vary by vendor.
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t appendCoordinate(
    std::uint64_t state,
    std::uint64_t domain,
    std::uint64_t value) {
    // Domain tags and a fresh mix after every field make order significant;
    // swapping seed/sequence or point/source cannot cancel as XOR terms.
    return mix(state ^ mix(domain) ^ mix(value));
}

double signedNoise(
    const OpenPortSweepPlan& plan,
    const ReceiverCoordinate& coordinate,
    std::uint64_t component) {
    auto key = appendCoordinate(0, 1, plan.seed);
    key = appendCoordinate(key, 2, plan.sequenceNumber);
    key = appendCoordinate(key, 3, coordinate.point);
    key = appendCoordinate(key, 4, coordinate.sourcePort);
    key = appendCoordinate(key, 5, coordinate.responsePort);
    key = appendCoordinate(key, 6, component);
    // A binary64 exactly represents the chosen 53-bit integer. Scaling it to
    // [-1, 1) therefore keeps the integer generator deterministic without a
    // hidden rounding step before conversion.
    constexpr double kUnit53 = 1.0 / 9007199254740992.0;
    return 2.0 * static_cast<double>(mix(key) >> 11U) * kUnit53 - 1.0;
}

double noiseScale(const OpenPortSweepPlan& plan) {
    const auto bandwidthFactor =
        std::sqrt(static_cast<double>(plan.ifBandwidthHz) / 1'000.0);
    const auto powerFactor = std::pow(10.0, (-10.0 - plan.powerDbm) / 20.0);
    return std::clamp(1.0e-5 * bandwidthFactor * powerFactor, 1.0e-8, 1.0e-3);
}

frames::ComplexSample receiverNoise(
    const OpenPortSweepPlan& plan,
    const ReceiverCoordinate& coordinate,
    double scale) {
    return frames::ComplexSample{
        .real = scale * signedNoise(plan, coordinate, 0),
        .imaginary = scale * signedNoise(plan, coordinate, 1),
    };
}

frames::RawReceiverSample makeSample(
    const OpenPortSweepPlan& plan,
    std::uint32_t point,
    std::uint32_t sourcePort) {
    const auto scale = noiseScale(plan);
    const auto referenceNoise = receiverNoise(
        plan, {point, sourcePort, 0}, scale * 0.1);
    frames::RawReceiverSample sample{
        .reference = {1.0 + referenceNoise.real, referenceNoise.imaginary},
        .responses = {},
    };
    sample.responses.reserve(plan.portCount);
    for (std::uint32_t responsePort = 1;
         responsePort <= plan.portCount;
         ++responsePort) {
        const auto noise = receiverNoise(
            plan, {point, sourcePort, responsePort}, scale);
        if (responsePort == sourcePort) {
            sample.responses.push_back({
                sample.reference.real + noise.real,
                sample.reference.imaginary + noise.imaginary,
            });
            continue;
        }
        const auto leakage = detail::coherentOpenPortLeakage(
            plan, point, sourcePort, responsePort);
        sample.responses.push_back({
            leakage.real + noise.real,
            leakage.imaginary + noise.imaginary,
        });
    }
    return sample;
}

frames::RawSourceState makeSourceState(
    const OpenPortSweepPlan& plan,
    std::uint32_t sourcePort) {
    frames::RawSourceState state{
        .sourcePort = sourcePort,
        .samples = {},
    };
    state.samples.reserve(plan.frequencyAxis.points);
    for (std::uint32_t point = 0;
         point < plan.frequencyAxis.points;
         ++point) {
        state.samples.push_back(makeSample(plan, point, sourcePort));
    }
    return state;
}

frames::RawSourceState makeLegacySourceState(
    std::uint32_t sourcePort,
    const frames::FrequencyAxis& axis) {
    frames::RawSourceState state{.sourcePort = sourcePort, .samples = {}};
    state.samples.reserve(axis.points);
    for (std::uint32_t index = 0; index < axis.points; ++index) {
        const auto position = static_cast<double>(index) /
                              static_cast<double>(axis.points - 1);
        std::vector<frames::ComplexSample> responses(2, {0.0, 0.0});
        responses[sourcePort - 1] = {
            .real = 0.5 - position,
            .imaginary = position * (1.0 - position),
        };
        state.samples.push_back({
            .reference = {1.0, 0.0},
            .responses = std::move(responses),
        });
    }
    return state;
}

}  // namespace

namespace detail {

frames::Result<std::vector<frames::RawReceiverSample>> simulateOpenPortRange(
    const OpenPortSweepPlan& plan,
    std::uint32_t sourcePort,
    std::uint32_t firstPoint,
    std::uint32_t pointCount) {
    if (const auto error = validatePlan(plan)) {
        return frames::Result<std::vector<frames::RawReceiverSample>>{*error};
    }
    if (sourcePort == 0 || sourcePort > plan.portCount) {
        return frames::Result<std::vector<frames::RawReceiverSample>>{
            frames::FrameError{frames::FrameErrorCode::InvalidSourcePort}};
    }
    if (pointCount == 0 || firstPoint >= plan.frequencyAxis.points ||
        pointCount > plan.frequencyAxis.points - firstPoint) {
        return frames::Result<std::vector<frames::RawReceiverSample>>{
            frames::FrameError{frames::FrameErrorCode::SampleCountMismatch}};
    }
    std::vector<frames::RawReceiverSample> samples;
    samples.reserve(pointCount);
    for (std::uint32_t point = firstPoint;
         point < firstPoint + pointCount;
         ++point) {
        samples.push_back(makeSample(plan, point, sourcePort));
    }
    return frames::Result<std::vector<frames::RawReceiverSample>>{
        std::move(samples)};
}

}  // namespace detail

frames::Result<frames::RawReceiverPayload> simulateOpenPorts(
    const OpenPortSweepPlan& plan) {
    if (const auto error = validatePlan(plan)) {
        return frames::Result<frames::RawReceiverPayload>{*error};
    }

    frames::RawReceiverPayload payload{
        .portCount = plan.portCount,
        .sourceStates = {},
    };
    payload.sourceStates.reserve(plan.portCount);
    for (std::uint32_t sourcePort = 1;
         sourcePort <= plan.portCount;
         ++sourcePort) {
        payload.sourceStates.push_back(makeSourceState(plan, sourcePort));
    }
    return frames::Result<frames::RawReceiverPayload>{std::move(payload)};
}

frames::Result<frames::RawReceiverPayload> simulateSweep(
    const frames::FrequencyAxis& frequencyAxis) {
    const OpenPortSweepPlan validation{
        .frequencyAxis = frequencyAxis,
        .portCount = 2,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
        .seed = 0,
        .sequenceNumber = 0,
    };
    if (const auto error = validatePlan(validation)) {
        return frames::Result<frames::RawReceiverPayload>{*error};
    }
    return frames::Result<frames::RawReceiverPayload>{frames::RawReceiverPayload{
        .portCount = 2,
        .sourceStates = {
            makeLegacySourceState(1, frequencyAxis),
            makeLegacySourceState(2, frequencyAxis),
        },
    }};
}

}  // namespace vna::simulation
