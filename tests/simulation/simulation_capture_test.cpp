#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <vna/compat/stop_token.hpp>
#include <variant>
#include <vector>

#include <vna/acquisition/raw_sweep_capture.hpp>
#include <vna/simulation/simulation_sweep.hpp>

namespace vna::simulation {
namespace {

using namespace std::chrono_literals;

acquisition::RawSweepCaptureRequest captureRequest() {
    return {
        .plan = {
            .frequencyAxis = {
                .id = frames::FrequencyAxisId{3},
                .startFrequencyHz = 1'000'000,
                .stopFrequencyHz = 5'000'000,
                .points = 5,
            },
            .portCount = 2,
            .sourcePorts = {1, 2},
            .ifBandwidthHz = 1'000,
            .powerDbm = -10.0,
        },
        .sweepId = acquisition::SweepId{19},
        .sequenceNumber = 7,
        .maximumPointsPerChunk = 2,
    };
}

OpenPortSweepPlan completePlan(
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

TEST(SimulationCaptureTest, PacesOrderedChunksAndMatchesCompleteSweep) {
    constexpr auto seed = 0x1234U;
    std::vector<std::chrono::steady_clock::duration> delays;
    std::vector<acquisition::RawSweepPointRange> observed;
    auto source = makeOpenPortSweepSource(
        {.seed = seed, .sweepDuration = 60ms},
        [&delays](auto delay, vna::compat::StopToken token) {
            delays.push_back(delay);
            return !token.stopRequested();
        });
    const auto request = captureRequest();

    const auto result = source(
        request,
        [&observed](const auto& chunk) { observed.push_back(chunk); }, {});

    const auto* payload =
        std::get_if<frames::RawReceiverPayload>(&result);
    ASSERT_NE(payload, nullptr);
    const auto expected = simulateOpenPorts(completePlan(request, seed));
    ASSERT_TRUE(expected.hasValue());
    EXPECT_EQ(*payload, expected.value());
    ASSERT_EQ(observed.size(), 6U);
    EXPECT_EQ(observed[0].sourcePort, 1U);
    EXPECT_EQ(observed[0].firstPoint, 0U);
    EXPECT_EQ(observed[2].firstPoint, 4U);
    EXPECT_EQ(observed[3].sourcePort, 2U);
    EXPECT_EQ(observed[5].firstPoint, 4U);
    ASSERT_EQ(delays.size(), observed.size());
    for (const auto delay : delays) {
        EXPECT_EQ(delay, 10ms);
    }
}

TEST(SimulationCaptureTest, DefaultsToOneSecondSweepPacing) {
    std::vector<std::chrono::steady_clock::duration> delays;
    auto source = makeOpenPortSweepSource(
        {.seed = 7},
        [&delays](auto delay, vna::compat::StopToken token) {
            delays.push_back(delay);
            return !token.stopRequested();
        });

    const auto result = source(captureRequest(), {}, {});

    EXPECT_TRUE(std::holds_alternative<frames::RawReceiverPayload>(result));
    ASSERT_EQ(delays.size(), 6U);
    const auto expectedDelay =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(1s) / 6;
    for (const auto delay : delays) {
        EXPECT_EQ(delay, expectedDelay);
    }
}

TEST(SimulationCaptureTest, ControlledPacerCancelsWithoutCompletePayload) {
    vna::compat::StopSource stop;
    int paceCalls = 0;
    int observed = 0;
    auto source = makeOpenPortSweepSource(
        {.seed = 7, .sweepDuration = 60ms},
        [&](auto, vna::compat::StopToken token) {
            if (++paceCalls == 2) {
                stop.requestStop();
            }
            return !token.stopRequested();
        });

    const auto result = source(
        captureRequest(),
        [&observed](const auto&) { ++observed; },
        stop.getToken());

    EXPECT_TRUE(std::holds_alternative<
                acquisition::RawSweepCaptureCanceled>(result));
    EXPECT_EQ(paceCalls, 2);
    EXPECT_EQ(observed, 1);
}

TEST(SimulationCaptureTest, DefaultPacerStopsAfterCaptureCallStarts) {
    auto steadyPacer = makeSteadySweepPacer();
    std::promise<void> pacing;
    auto pacingStarted = pacing.get_future();
    auto source = makeOpenPortSweepSource(
        {.seed = 7, .sweepDuration = 18s},
        [steadyPacer = std::move(steadyPacer), &pacing](
            auto delay, vna::compat::StopToken token) {
            pacing.set_value();
            return steadyPacer(delay, token);
        });
    vna::compat::StopSource stop;

    auto result = std::async(std::launch::async, [&] {
        return source(captureRequest(), {}, stop.getToken());
    });
    if (pacingStarted.wait_for(1s) != std::future_status::ready) {
        stop.requestStop();
        FAIL() << "capture did not enter the production pacer";
    }
    stop.requestStop();

    ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(std::holds_alternative<
                acquisition::RawSweepCaptureCanceled>(result.get()));
}

}  // namespace
}  // namespace vna::simulation
