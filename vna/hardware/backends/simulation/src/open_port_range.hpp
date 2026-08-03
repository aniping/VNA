#pragma once

#include <cstdint>
#include <vector>

#include <vna/simulation/simulation_sweep.hpp>

namespace vna::simulation::detail {

[[nodiscard]] frames::Result<std::vector<frames::RawReceiverSample>>
simulateOpenPortRange(
    const OpenPortSweepPlan& plan,
    std::uint32_t sourcePort,
    std::uint32_t firstPoint,
    std::uint32_t pointCount);

}  // namespace vna::simulation::detail
