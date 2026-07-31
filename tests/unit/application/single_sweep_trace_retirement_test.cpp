#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stop_token>
#include <utility>
#include <variant>

#include <vna/application/single_sweep_executor.hpp>
#include <vna/simulation/simulation_sweep.hpp>

#include "single_sweep_executor_test_support.hpp"

namespace vna::application {
namespace {

using namespace std::chrono_literals;
using test_support::acceptedOperation;
using test_support::awaitTerminal;
using test_support::validWorkItem;

TraceDisplayFrame replacementFrame() {
    return TraceDisplayFrame{
        .frameId = frames::FrameId{31},
        .traceId = display_model::TraceId{4},
        .stateRevision = 8,
        .sequenceNumber = 1,
        .format = display_model::TraceFormat::LogMagnitude,
        .valueUnit = display_model::ScaleUnit::Decibel,
        .frequenciesHz = {1'000'000.0, 2'000'000.0},
        .values = {-6.0, -3.0},
    };
}

RawSweepSource simulationSource() {
    return [](const frames::FrequencyAxis& axis, std::stop_token) {
        return simulation::simulateSweep(axis);
    };
}

TEST(SingleSweepTraceRetirementTest, RunningSweepCannotPublishAfterRetirement) {
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
    if (enteredFuture.wait_for(2s) != std::future_status::ready) {
        release.set_value();
        FAIL() << "source did not start";
        return;
    }

    executor.discardTrace(display_model::TraceId{3});
    release.set_value();
    const auto terminal = awaitTerminal(manager, submitted);

    EXPECT_TRUE(std::holds_alternative<OperationCanceled>(terminal.state));
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
    EXPECT_TRUE(repository.publish(replacementFrame()).hasValue());
}

TEST(SingleSweepTraceRetirementTest, PublishingFinishesBeforeRetirementReturns) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    TraceDisplayPublisher publisher = [&](TraceDisplayFrame frame) {
        entered.set_value();
        releaseFuture.wait();
        return repository.publish(std::move(frame));
    };
    SingleSweepExecutor executor{
        1, simulationSource(), manager, repository, std::move(publisher)};
    const auto submitted =
        acceptedOperation(manager, executor.submit(validWorkItem()));
    if (enteredFuture.wait_for(2s) != std::future_status::ready) {
        release.set_value();
        FAIL() << "publisher did not start";
        return;
    }
    TraceDisplayFrameHandle visibleAtCompletion;
    std::promise<void> completed;
    auto completedFuture = completed.get_future();
    auto fence = manager.captureFence(submitted.sessionId);
    auto subscription = manager.subscribe(
        std::move(fence),
        [&] {
            visibleAtCompletion =
                repository.latest(display_model::TraceId{3});
            completed.set_value();
        });
    auto retirement = std::async(std::launch::async, [&] {
        executor.discardTrace(display_model::TraceId{3});
    });
    EXPECT_EQ(retirement.wait_for(0ms), std::future_status::timeout);

    release.set_value();
    retirement.get();
    EXPECT_EQ(completedFuture.wait_for(2s), std::future_status::ready);
    const auto terminal = std::get<OperationSnapshot>(
        manager.snapshot(submitted.id));
    executor.stop();

    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(terminal.state));
    EXPECT_NE(visibleAtCompletion, nullptr);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
    EXPECT_TRUE(repository.publish(replacementFrame()).hasValue());
}

}  // namespace
}  // namespace vna::application
