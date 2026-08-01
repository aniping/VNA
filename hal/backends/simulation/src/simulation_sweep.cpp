#include <vna/simulation/simulation_sweep.hpp>

#include <optional>
#include <utility>
#include <vector>

namespace vna::simulation {
namespace {

std::optional<frames::FrameError> validateAxis(
    const frames::FrequencyAxis& axis) {
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
    return std::nullopt;
}

frames::RawSourceState makeSourceState(
    std::uint32_t sourcePort,
    const frames::FrequencyAxis& axis) {
    frames::RawSourceState state{
        .sourcePort = sourcePort,
        .samples = {},
    };
    state.samples.reserve(axis.points);
    for (std::uint32_t index = 0; index < axis.points; ++index) {
        const auto position = static_cast<double>(index) /
                              static_cast<double>(axis.points - 1);
        const frames::ComplexSample reflected{
            .real = 0.5 - position,
            .imaginary = position * (1.0 - position),
        };
        std::vector<frames::ComplexSample> responses(2, {0.0, 0.0});
        responses[sourcePort - 1] = reflected;
        state.samples.push_back(frames::RawReceiverSample{
            .reference = {1.0, 0.0},
            .responses = std::move(responses),
        });
    }
    return state;
}

}  // namespace

frames::Result<frames::RawReceiverPayload> simulateSweep(
    const frames::FrequencyAxis& frequencyAxis) {
    if (const auto error = validateAxis(frequencyAxis)) {
        return frames::Result<frames::RawReceiverPayload>{*error};
    }

    return frames::Result<frames::RawReceiverPayload>{
        frames::RawReceiverPayload{
            .portCount = 2,
            .sourceStates = {
                makeSourceState(1, frequencyAxis),
                makeSourceState(2, frequencyAxis),
            },
        }};
}

}  // namespace vna::simulation
