#pragma once

#include <vna/frames/frames.hpp>

namespace vna::simulation {

// Generates deterministic receiver samples only. The application coordinator
// remains responsible for attaching frame identity and state revision so this
// backend cannot publish data outside the sweep that requested it.
[[nodiscard]] frames::Result<frames::RawReceiverPayload> simulateSweep(
    const frames::FrequencyAxis& frequencyAxis);

}  // namespace vna::simulation
