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

}  // namespace

frames::Result<frames::RawReceiverPayload> simulateSweep(
    const frames::FrequencyAxis& frequencyAxis) {
    if (const auto error = validateAxis(frequencyAxis)) {
        return frames::Result<frames::RawReceiverPayload>{*error};
    }

    std::vector<frames::RawReceiverSample> samples;
    samples.reserve(frequencyAxis.points);

    // This simple reflection curve is deliberately algebraic and noiseless.
    // Its binary-exact five-point values make cross-platform tests stable while
    // still exercising both real and imaginary receiver components.
    for (std::uint32_t index = 0; index < frequencyAxis.points; ++index) {
        const auto position = static_cast<double>(index) /
                              static_cast<double>(frequencyAxis.points - 1);
        samples.push_back(frames::RawReceiverSample{
            .a1 = {.real = 1.0, .imaginary = 0.0},
            .b1 = {
                .real = 0.5 - position,
                .imaginary = position * (1.0 - position),
            },
        });
    }

    return frames::Result<frames::RawReceiverPayload>{
        frames::RawReceiverPayload{.samples = std::move(samples)}};
}

}  // namespace vna::simulation
