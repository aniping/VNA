#pragma once

#include <cstdint>

#include <vna/simulation/simulation_sweep.hpp>

namespace vna::simulation::detail {

[[nodiscard]] frames::ComplexSample coherentOpenPortLeakage(
    const OpenPortSweepPlan& plan,
    std::uint32_t point,
    std::uint32_t sourcePort,
    std::uint32_t responsePort);

}  // namespace vna::simulation::detail
