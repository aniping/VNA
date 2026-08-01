#pragma once

#include <vna/frames/raw_receiver.hpp>

namespace vna::simulation {

struct OpenPortSweepPlan {
    frames::FrequencyAxis frequencyAxis;
    std::uint32_t portCount;
    std::uint64_t ifBandwidthHz;
    double powerDbm;
    std::uint64_t seed;
    std::uint64_t sequenceNumber;
};

// Produces every source state and response receiver for an open-port fixture.
// The plan intentionally has no Channel or Trace identity: a continuous
// acquisition owner can advance sequence independently of application state.
[[nodiscard]] frames::Result<frames::RawReceiverPayload> simulateOpenPorts(
    const OpenPortSweepPlan& plan);

// Generates deterministic receiver samples only. The application coordinator
// remains responsible for attaching frame identity and state revision so this
// backend cannot publish data outside the sweep that requested it. This legacy
// entry preserves the existing single-sweep fixture during source migration.
[[nodiscard]] frames::Result<frames::RawReceiverPayload> simulateSweep(
    const frames::FrequencyAxis& frequencyAxis);

}  // namespace vna::simulation
