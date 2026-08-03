#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <vna/compat/stop_token.hpp>
#include <stdexcept>
#include <utility>
#include <variant>

#include <vna/application/single_sweep_executor.hpp>
#include <vna/simulation/simulation_sweep.hpp>

#include "single_sweep_executor_test_support.hpp"

namespace vna::application {
namespace {

using test_support::awaitTerminal;
using test_support::acceptedOperation;
using test_support::validWorkItem;

RawSweepSource simulationSource() {
    return [](const frames::FrequencyAxis& axis, vna::compat::StopToken) {
        return simulation::simulateSweep(axis);
    };
}

void expectFailure(
    RawSweepSource source,
    SingleSweepWorkItem work,
    SingleSweepFailureCode expected,
    std::function<void(const OperationFailure&)> inspect = [](const auto&) {}) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    const auto traceId = work.traceId;
    SingleSweepExecutor executor{
        1, std::move(source), manager, repository};

    const auto submitted =
        acceptedOperation(manager, executor.submit(std::move(work)));
    const auto terminal = awaitTerminal(manager, submitted);

    const auto* failed = std::get_if<OperationFailed>(&terminal.state);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(failed->error.code, expected);
    inspect(failed->error);
    EXPECT_EQ(repository.latest(traceId), nullptr);
}

TEST(SingleSweepExecutorFailureTest, RawSourceFailureIsAtomic) {
    RawSweepSource source = [](const frames::FrequencyAxis&,
                               vna::compat::StopToken) {
        return frames::Result<frames::RawReceiverPayload>{frames::FrameError{
            .code = frames::FrameErrorCode::InvalidFrequencyAxis}};
    };

    expectFailure(
        std::move(source), validWorkItem(),
        SingleSweepFailureCode::RawSweepFailed,
        [](const OperationFailure& failure) {
            const auto* cause = failure.cause.getIf<frames::FrameError>();
            ASSERT_NE(cause, nullptr);
            EXPECT_EQ(cause->code, frames::FrameErrorCode::InvalidFrequencyAxis);
        });
}

TEST(SingleSweepExecutorFailureTest, RawSourceExceptionIsPreserved) {
    RawSweepSource source = [](const frames::FrequencyAxis&,
                               vna::compat::StopToken)
        -> frames::Result<frames::RawReceiverPayload> {
        throw std::runtime_error{"source failed"};
    };

    expectFailure(
        std::move(source), validWorkItem(),
        SingleSweepFailureCode::RawSweepFailed,
        [](const OperationFailure& failure) {
            const auto* cause = failure.cause.getIf<std::exception_ptr>();
            ASSERT_NE(cause, nullptr);
            EXPECT_THROW(std::rethrow_exception(*cause), std::runtime_error);
        });
}

TEST(SingleSweepExecutorFailureTest, RawFrameRejectionIsAtomic) {
    RawSweepSource source = [](const frames::FrequencyAxis& axis,
                               vna::compat::StopToken) {
        auto payload = simulation::simulateSweep(axis).value();
        payload.sourceStates.front().samples.pop_back();
        return frames::Result<frames::RawReceiverPayload>{std::move(payload)};
    };

    expectFailure(
        std::move(source), validWorkItem(),
        SingleSweepFailureCode::RawFrameRejected);
}

TEST(SingleSweepExecutorFailureTest, SynthesisFailureIsAtomic) {
    auto work = validWorkItem();
    work.measurement.type = domain::MeasurementType::S21;

    expectFailure(
        simulationSource(), std::move(work),
        SingleSweepFailureCode::MeasurementSynthesisFailed);
}

TEST(SingleSweepExecutorFailureTest, ProjectionFailureIsAtomic) {
    RawSweepSource source = [](const frames::FrequencyAxis& axis,
                               vna::compat::StopToken) {
        auto payload = simulation::simulateSweep(axis).value();
        payload.sourceStates.front().samples.front().responses.front() =
            {.real = 0.0, .imaginary = 0.0};
        return frames::Result<frames::RawReceiverPayload>{std::move(payload)};
    };

    expectFailure(
        std::move(source), validWorkItem(),
        SingleSweepFailureCode::LogMagnitudeProjectionFailed);
}

TEST(SingleSweepExecutorFailureTest, FrequencyRoundingFailureIsAtomic) {
    auto work = validWorkItem();
    work.frequencyAxis.startFrequencyHz =
        std::numeric_limits<std::uint64_t>::max() - 1;
    work.frequencyAxis.stopFrequencyHz =
        std::numeric_limits<std::uint64_t>::max();
    work.frequencyAxis.points = 2;

    expectFailure(
        simulationSource(), std::move(work),
        SingleSweepFailureCode::FrequencyMaterializationFailed,
        [](const OperationFailure& failure) {
            EXPECT_TRUE(failure.cause.holds<std::monostate>());
        });
}

TEST(SingleSweepExecutorFailureTest, PublishFailureKeepsExistingFrame) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    const auto existing = repository.publish(TraceDisplayFrame{
        .frameId = frames::FrameId{40},
        .traceId = display_model::TraceId{9},
        .measurementId = domain::MeasurementId{9},
        .measurementType = domain::MeasurementType::S11,
        .stateRevision = 6,
        .generation = 1,
        .sequenceNumber = 1,
        .format = display_model::TraceFormat::LogMagnitude,
        .frequenciesHz = {1.0, 2.0},
        .samples = CartesianTraceDisplaySamples{
            .unit = TraceDisplayUnit::Decibel,
            .values = {-1.0, -2.0}},
    });
    ASSERT_TRUE(existing.hasValue());
    SingleSweepExecutor executor{
        1, simulationSource(), manager, repository};

    const auto submitted =
        acceptedOperation(manager, executor.submit(validWorkItem()));
    const auto terminal = awaitTerminal(manager, submitted);

    const auto* failed = std::get_if<OperationFailed>(&terminal.state);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(
        failed->error.code,
        SingleSweepFailureCode::TraceDisplayPublishFailed);
    const auto* cause =
        failed->error.cause.getIf<TraceDisplayFrameError>();
    ASSERT_NE(cause, nullptr);
    EXPECT_EQ(cause->code, TraceDisplayFrameErrorCode::CapacityExceeded);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
    EXPECT_EQ(repository.latest(display_model::TraceId{9}), existing.value());
}

TEST(SingleSweepExecutorFailureTest, PublisherExceptionDoesNotStopWorker) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    int attempts = 0;
    TraceDisplayPublisher publisher = [&](TraceDisplayFrame frame) {
        if (++attempts == 1) {
            throw std::runtime_error{"repository allocation failed"};
        }
        return repository.publish(std::move(frame));
    };
    SingleSweepExecutor executor{
        1, simulationSource(), manager, repository, std::move(publisher)};

    const auto first =
        acceptedOperation(manager, executor.submit(validWorkItem()));
    const auto failedTerminal = awaitTerminal(manager, first);
    const auto* failed =
        std::get_if<OperationFailed>(&failedTerminal.state);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(
        failed->error.code,
        SingleSweepFailureCode::TraceDisplayPublishFailed);
    EXPECT_NE(failed->error.cause.getIf<std::exception_ptr>(), nullptr);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);

    auto secondWork = validWorkItem(CommandId{"sweep-2"});
    secondWork.frameContext.frameId = frames::FrameId{12};
    secondWork.frameContext.sequenceNumber = 2;
    const auto second = acceptedOperation(
        manager, executor.submit(std::move(secondWork)));
    const auto succeededTerminal = awaitTerminal(manager, second);

    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(
        succeededTerminal.state));
    EXPECT_NE(repository.latest(display_model::TraceId{3}), nullptr);
}

}  // namespace
}  // namespace vna::application
