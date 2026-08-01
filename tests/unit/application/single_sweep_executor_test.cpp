#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <variant>
#include <vector>

#include <vna/application/single_sweep_executor.hpp>
#include <vna/simulation/simulation_sweep.hpp>

#include "single_sweep_executor_test_support.hpp"

namespace vna::application {
namespace {

using namespace std::chrono_literals;
using test_support::acceptedOperation;
using test_support::awaitTerminal;
using test_support::validWorkItem;

TEST(SingleSweepExecutorTest, PublishesFivePointGoldenBeforeSuccess) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    RawSweepSource source = [](const frames::FrequencyAxis& axis,
                               std::stop_token) {
        return simulation::simulateSweep(axis);
    };
    SingleSweepExecutor executor{1, std::move(source), manager, repository};

    const auto submitted = executor.submit(validWorkItem());

    const auto* operationId = std::get_if<OperationId>(&submitted);
    ASSERT_NE(operationId, nullptr);
    const auto accepted = std::get<OperationSnapshot>(manager.snapshot(*operationId));
    EXPECT_TRUE(std::holds_alternative<OperationQueued>(accepted.state));
    EXPECT_EQ(accepted.commandId, CommandId{"sweep-1"});
    EXPECT_EQ(accepted.sessionId, SessionId{"session-1"});
    EXPECT_EQ(accepted.submittedAtStateRevision, 7U);
    TraceDisplayFrameHandle visibleAtCompletion;
    const auto terminal = awaitTerminal(manager, accepted, [&] {
        visibleAtCompletion = repository.latest(display_model::TraceId{3});
    });

    const auto* succeeded = std::get_if<OperationSucceeded>(&terminal.state);
    ASSERT_NE(succeeded, nullptr);
    EXPECT_EQ(succeeded->frameId, frames::FrameId{11});
    ASSERT_NE(visibleAtCompletion, nullptr);
    EXPECT_EQ(visibleAtCompletion->frameId, frames::FrameId{11});
    EXPECT_EQ(visibleAtCompletion->traceId, display_model::TraceId{3});
    EXPECT_EQ(visibleAtCompletion->stateRevision, 7U);
    EXPECT_EQ(visibleAtCompletion->sequenceNumber, 1U);
    EXPECT_EQ(visibleAtCompletion->format,
              display_model::TraceFormat::LogMagnitude);
    EXPECT_EQ(visibleAtCompletion->measurementId, domain::MeasurementId{1});
    const auto& samples = std::get<CartesianTraceDisplaySamples>(
        visibleAtCompletion->samples);
    EXPECT_EQ(samples.unit, TraceDisplayUnit::Decibel);
    EXPECT_EQ(visibleAtCompletion->frequenciesHz,
              (std::vector<double>{
                  1'000'000.0, 1'250'000.0, 1'500'000.0,
                  1'750'000.0, 2'000'000.0}));
    const double expected[] = {
        -6.020599913279624, -10.102999566398122, -12.041199826559248,
        -10.102999566398122, -6.020599913279624};
    ASSERT_EQ(samples.values.size(), std::size(expected));
    for (std::size_t index = 0; index < std::size(expected); ++index) {
        EXPECT_NEAR(samples.values[index], expected[index], 1e-12);
    }
}

TEST(SingleSweepExecutorTest, RejectsInvalidConstruction) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    RawSweepSource empty;

    EXPECT_THROW(
        SingleSweepExecutor(1, std::move(empty), manager, repository),
        std::invalid_argument);
    RawSweepSource source = [](const frames::FrequencyAxis& axis,
                               std::stop_token) {
        return simulation::simulateSweep(axis);
    };
    TraceDisplayPublisher emptyPublisher;
    EXPECT_THROW(
        SingleSweepExecutor(
            1, source, manager, repository, std::move(emptyPublisher)),
        std::invalid_argument);
    EXPECT_THROW(
        SingleSweepExecutor(0, std::move(source), manager, repository),
        std::invalid_argument);
}

TEST(SingleSweepExecutorTest, RejectsFullQueueWithoutCreatingOperation) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    std::atomic<int> calls{0};
    RawSweepSource source = [&](const frames::FrequencyAxis& axis,
                                std::stop_token) {
        if (calls.fetch_add(1) == 0) {
            entered.set_value();
            releaseFuture.wait();
        }
        return simulation::simulateSweep(axis);
    };
    SingleSweepExecutor executor{1, std::move(source), manager, repository};

    const auto first = acceptedOperation(
        manager, executor.submit(validWorkItem(CommandId{"sweep-1"})));
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);
    const auto second = acceptedOperation(
        manager, executor.submit(validWorkItem(CommandId{"sweep-2"})));
    const auto rejected = executor.submit(
        validWorkItem(CommandId{"sweep-rejected"}));

    const auto* error = std::get_if<SingleSweepSubmitError>(&rejected);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, SingleSweepSubmitErrorCode::QueueFull);
    EXPECT_EQ(calls.load(), 1);
    release.set_value();
    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(
        awaitTerminal(manager, first).state));
    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(
        awaitTerminal(manager, second).state));
    const auto after = acceptedOperation(
        manager, executor.submit(validWorkItem(CommandId{"sweep-3"})));
    EXPECT_EQ(after.id, OperationId{3});
    (void)awaitTerminal(manager, after);
}

TEST(SingleSweepExecutorTest, RejectsSubmissionAfterStopWithoutOperation) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    RawSweepSource source = [](const frames::FrequencyAxis& axis,
                               std::stop_token) {
        return simulation::simulateSweep(axis);
    };
    SingleSweepExecutor executor{1, std::move(source), manager, repository};
    executor.stop();

    const auto rejected = executor.submit(validWorkItem());

    const auto* error = std::get_if<SingleSweepSubmitError>(&rejected);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, SingleSweepSubmitErrorCode::Stopped);
    const auto proof = manager.create(OperationSubmission{
        CommandId{"proof"}, SessionId{"session-1"}, 7});
    EXPECT_EQ(proof.id, OperationId{1});
}

TEST(SingleSweepExecutorTest, CancelsOnlyAfterSourceReleases) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    RawSweepSource source = [&](const frames::FrequencyAxis& axis,
                                std::stop_token) {
        entered.set_value();
        releaseFuture.wait();
        return simulation::simulateSweep(axis);
    };
    SingleSweepExecutor executor{1, std::move(source), manager, repository};
    const auto submitted =
        acceptedOperation(manager, executor.submit(validWorkItem()));
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);

    const auto requested = manager.requestCancel(submitted.id);

    EXPECT_TRUE(std::holds_alternative<OperationCancelRequested>(
        std::get<OperationSnapshot>(requested).state));
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
    release.set_value();
    const auto terminal = awaitTerminal(manager, submitted);
    EXPECT_TRUE(std::holds_alternative<OperationCanceled>(terminal.state));
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
}

TEST(SingleSweepExecutorTest, DestructorStopsSourceAndJoinsBeforeReturning) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::mutex sourceMutex;
    std::condition_variable sourceCondition;
    std::atomic<int> calls{0};
    std::optional<OperationSnapshot> running;
    std::optional<OperationSnapshot> queued;
    {
        RawSweepSource source = [&](const frames::FrequencyAxis& axis,
                                    std::stop_token token) {
            ++calls;
            entered.set_value();
            std::stop_callback notify{token, [&] {
                sourceCondition.notify_all();
            }};
            std::unique_lock lock{sourceMutex};
            sourceCondition.wait(lock, [&] { return token.stop_requested(); });
            return simulation::simulateSweep(axis);
        };
        SingleSweepExecutor executor{1, std::move(source), manager, repository};
        running = acceptedOperation(
            manager, executor.submit(validWorkItem()));
        ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);
        queued = acceptedOperation(
            manager,
            executor.submit(validWorkItem(CommandId{"sweep-queued"})));
    }

    const auto runningTerminal = std::get<OperationSnapshot>(
        manager.snapshot(running->id));
    const auto queuedTerminal = std::get<OperationSnapshot>(
        manager.snapshot(queued->id));
    EXPECT_TRUE(
        std::holds_alternative<OperationCanceled>(runningTerminal.state));
    EXPECT_TRUE(
        std::holds_alternative<OperationCanceled>(queuedTerminal.state));
    EXPECT_EQ(calls.load(), 1);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
}

}  // namespace
}  // namespace vna::application
