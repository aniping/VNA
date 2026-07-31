#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <stop_token>
#include <utility>
#include <variant>
#include <vector>

#include <vna/application/single_sweep_executor.hpp>
#include <vna/simulation/simulation_sweep.hpp>

namespace vna::application {
namespace {

using namespace std::chrono_literals;

SingleSweepWorkItem validWorkItem(
    CommandId commandId = CommandId{"sweep-1"},
    display_model::TraceId traceId = display_model::TraceId{3}) {
    return SingleSweepWorkItem{
        .commandId = std::move(commandId),
        .sessionId = SessionId{"session-1"},
        .frameContext = {
            .frameId = frames::FrameId{11},
            .sweepId = frames::SweepId{21},
            .channelId = domain::ChannelId{1},
            .stateRevision = 7,
            .sequenceNumber = 1,
        },
        .frequencyAxis = {
            .id = frames::FrequencyAxisId{31},
            .startFrequencyHz = 1'000'000,
            .stopFrequencyHz = 2'000'000,
            .points = 5,
        },
        .measurement = {
            .id = domain::MeasurementId{1},
            .channelId = domain::ChannelId{1},
            .type = domain::MeasurementType::S11,
        },
        .traceId = traceId,
    };
}

OperationSnapshot awaitTerminal(
    OperationManager& manager,
    const OperationSnapshot& submitted,
    std::function<void()> atCompletion = [] {}) {
    std::promise<void> completed;
    auto future = completed.get_future();
    auto fence = manager.captureFence(submitted.sessionId);
    auto subscription = manager.subscribe(
        std::move(fence),
        [&] {
            atCompletion();
            completed.set_value();
        });
    if (future.wait_for(2s) != std::future_status::ready) {
        ADD_FAILURE() << "operation did not reach a terminal state";
    }
    return std::get<OperationSnapshot>(manager.snapshot(submitted.id));
}

TEST(SingleSweepExecutorTest, PublishesFivePointGoldenBeforeSuccess) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    RawSweepSource source = [](const frames::FrequencyAxis& axis,
                               std::stop_token) {
        return simulation::simulateSweep(axis);
    };
    SingleSweepExecutor executor{1, std::move(source), manager, repository};

    const auto submitted = executor.submit(validWorkItem());

    const auto* accepted = std::get_if<OperationSnapshot>(&submitted);
    ASSERT_NE(accepted, nullptr);
    EXPECT_TRUE(std::holds_alternative<OperationQueued>(accepted->state));
    EXPECT_EQ(accepted->commandId, CommandId{"sweep-1"});
    EXPECT_EQ(accepted->sessionId, SessionId{"session-1"});
    EXPECT_EQ(accepted->submittedAtStateRevision, 7U);
    TraceDisplayFrameHandle visibleAtCompletion;
    const auto terminal = awaitTerminal(manager, *accepted, [&] {
        visibleAtCompletion = repository.latest(display_model::TraceId{3});
    });

    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(terminal.state));
    ASSERT_NE(visibleAtCompletion, nullptr);
    EXPECT_EQ(visibleAtCompletion->frameId, frames::FrameId{11});
    EXPECT_EQ(visibleAtCompletion->traceId, display_model::TraceId{3});
    EXPECT_EQ(visibleAtCompletion->stateRevision, 7U);
    EXPECT_EQ(visibleAtCompletion->sequenceNumber, 1U);
    EXPECT_EQ(visibleAtCompletion->format,
              display_model::TraceFormat::LogMagnitude);
    EXPECT_EQ(visibleAtCompletion->valueUnit,
              display_model::ScaleUnit::Decibel);
    EXPECT_EQ(visibleAtCompletion->frequenciesHz,
              (std::vector<double>{
                  1'000'000.0, 1'250'000.0, 1'500'000.0,
                  1'750'000.0, 2'000'000.0}));
    const double expected[] = {
        -6.020599913279624, -10.102999566398122, -12.041199826559248,
        -10.102999566398122, -6.020599913279624};
    ASSERT_EQ(visibleAtCompletion->values.size(), std::size(expected));
    for (std::size_t index = 0; index < std::size(expected); ++index) {
        EXPECT_NEAR(visibleAtCompletion->values[index], expected[index], 1e-12);
    }
}

}  // namespace
}  // namespace vna::application
