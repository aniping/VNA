#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <stop_token>

#include <vna/acquisition/raw_sweep_capture.hpp>
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

struct OpenPortSweepSourceOptions {
    std::uint64_t seed;
    std::chrono::steady_clock::duration sweepDuration{
        std::chrono::milliseconds{100}};
};

// A pacer is a simulation-time adapter, not a second worker. Production uses
// a stop-aware steady-clock wait; tests can inject a deterministic gate.
using SimulationSweepPacer = std::function<bool(
    std::chrono::steady_clock::duration,
    std::stop_token)>;

[[nodiscard]] SimulationSweepPacer makeSteadySweepPacer();

[[nodiscard]] acquisition::RawSweepCaptureSource makeOpenPortSweepSource(
    OpenPortSweepSourceOptions options,
    SimulationSweepPacer pacer = {});

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
