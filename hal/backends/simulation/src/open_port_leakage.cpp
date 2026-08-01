#include "open_port_leakage.hpp"

#include <algorithm>
#include <cstdint>

namespace vna::simulation::detail {
namespace {

frames::ComplexSample rotateForPortPair(
    frames::ComplexSample value,
    std::uint32_t sourcePort,
    std::uint32_t responsePort) {
    // A port-pair-derived quadrant distinguishes coupling phase without
    // changing the shared isolation envelope for two- and four-port plans.
    switch ((3 * sourcePort + responsePort) % 4) {
    case 0:
        return value;
    case 1:
        return {-value.imaginary, value.real};
    case 2:
        return {-value.real, -value.imaginary};
    default:
        return {value.imaginary, -value.real};
    }
}

}  // namespace

frames::ComplexSample coherentOpenPortLeakage(
    const OpenPortSweepPlan& plan,
    std::uint32_t point,
    std::uint32_t sourcePort,
    std::uint32_t responsePort) {
    // Open connectors are not a through path, but a real instrument still has
    // repeatable internal coupling. This bounded curve represents two broad
    // coupling paths across the 26.5 GHz product band without inventing a DUT
    // resonance. Noise is added elsewhere so IFBW and power cannot move it.
    constexpr double productStopFrequencyHz = 26'500'000'000.0;
    constexpr double couplingScale = 2.0e-3;
    const auto position = static_cast<double>(point) /
                          static_cast<double>(plan.frequencyAxis.points - 1);
    const auto frequencyHz =
        static_cast<double>(plan.frequencyAxis.startFrequencyHz) +
        (static_cast<double>(plan.frequencyAxis.stopFrequencyHz) -
         static_cast<double>(plan.frequencyAxis.startFrequencyHz)) *
            position;
    const auto frequency =
        std::clamp(frequencyHz / productStopFrequencyHz, 0.0, 1.0);
    const auto bend = frequency * (1.0 - frequency);
    const auto separation = std::max(
        sourcePort > responsePort ? sourcePort - responsePort
                                  : responsePort - sourcePort,
        std::uint32_t{1});
    const auto pairScale = couplingScale /
                           (1.0 + 0.18 * static_cast<double>(separation - 1));
    return rotateForPortPair(
        {pairScale * (0.65 + 0.35 * frequency + 1.2 * bend),
         pairScale * (-0.5 + frequency - 0.8 * bend)},
        sourcePort,
        responsePort);
}

}  // namespace vna::simulation::detail
