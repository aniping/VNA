#include <vna/simulation/simulation_sweep.hpp>

#include "open_port_range.hpp"

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace vna::simulation {
namespace {

bool waitFor(
    std::chrono::steady_clock::duration delay,
    vna::compat::StopToken token) {
    if (delay <= decltype(delay)::zero()) {
        return !token.stopRequested();
    }
    std::mutex mutex;
    std::condition_variable changed;
    vna::compat::StopCallback notify{token, [&] {
        // The predicate and notification share this mutex, closing the
        // check-to-sleep window without a polling interval.
        std::lock_guard lock{mutex};
        changed.notify_all();
    }};
    std::unique_lock lock{mutex};
    changed.wait_for(lock, delay, [&] { return token.stopRequested(); });
    return !token.stopRequested();
}

std::uint64_t chunkCount(
    const acquisition::RawSweepCaptureRequest& request) {
    if (request.maximumPointsPerChunk == 0) {
        return 0;
    }
    const auto points = request.plan.frequencyAxis.points;
    const auto perState =
        (points + request.maximumPointsPerChunk - 1) /
        request.maximumPointsPerChunk;
    return perState * request.plan.sourcePorts.size();
}

OpenPortSweepPlan openPortPlan(
    const acquisition::RawSweepCaptureRequest& request,
    std::uint64_t seed) {
    return {
        .frequencyAxis = request.plan.frequencyAxis,
        .portCount = request.plan.portCount,
        .ifBandwidthHz = request.plan.ifBandwidthHz,
        .powerDbm = request.plan.powerDbm,
        .seed = seed,
        .sequenceNumber = request.sequenceNumber,
    };
}

}  // namespace

SimulationSweepPacer makeSteadySweepPacer() {
    return waitFor;
}

acquisition::RawSweepCaptureSource makeOpenPortSweepSource(
    OpenPortSweepSourceOptions options,
    SimulationSweepPacer pacer) {
    if (options.sweepDuration < decltype(options.sweepDuration)::zero()) {
        throw std::invalid_argument{"simulation sweep duration is negative"};
    }
    if (!pacer) {
        pacer = makeSteadySweepPacer();
    }
    return [options, pacer = std::move(pacer)](
               const acquisition::RawSweepCaptureRequest& request,
               const acquisition::RawSweepChunkObserver& observer,
               vna::compat::StopToken token) {
        const auto count = chunkCount(request);
        auto delay = decltype(options.sweepDuration)::zero();
        if (count != 0) {
            delay = options.sweepDuration /
                    static_cast<decltype(options.sweepDuration)::rep>(count);
        }
        const auto plan = openPortPlan(request, options.seed);
        acquisition::RawSweepChunkProducer producer =
            [plan, delay, &pacer](
                const acquisition::ContinuousAcquisitionPlan&,
                acquisition::RawSweepChunkRequest chunk,
                vna::compat::StopToken chunkToken)
                -> acquisition::RawSweepChunkResult {
                if (!pacer(delay, chunkToken)) {
                    return acquisition::RawSweepCaptureCanceled{};
                }
                auto samples = detail::simulateOpenPortRange(
                    plan, chunk.sourcePort, chunk.firstPoint, chunk.pointCount);
                if (!samples.hasValue()) {
                    return samples.error();
                }
                return acquisition::RawSweepPointRange{
                    .sourcePort = chunk.sourcePort,
                    .firstPoint = chunk.firstPoint,
                    .samples = samples.value(),
                };
            };
        return acquisition::captureRawSweep(
            request, producer, observer, token);
    };
}

}  // namespace vna::simulation
