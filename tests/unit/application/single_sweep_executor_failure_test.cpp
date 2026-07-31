#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <stop_token>
#include <stdexcept>
#include <utility>
#include <variant>

#include <vna/application/single_sweep_executor.hpp>
#include <vna/simulation/simulation_sweep.hpp>

#include "single_sweep_executor_test_support.hpp"

namespace vna::application {
namespace {

using test_support::awaitTerminal;
using test_support::validWorkItem;

RawSweepSource simulationSource() {
    return [](const frames::FrequencyAxis& axis, std::stop_token) {
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
        std::get<OperationSnapshot>(executor.submit(std::move(work)));
    const auto terminal = awaitTerminal(manager, submitted);

    const auto* failed = std::get_if<OperationFailed>(&terminal.state);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(failed->error.code, expected);
    inspect(failed->error);
    EXPECT_EQ(repository.latest(traceId), nullptr);
}

TEST(SingleSweepExecutorFailureTest, RawSourceFailureIsAtomic) {
    RawSweepSource source = [](const frames::FrequencyAxis&,
                               std::stop_token) {
        return frames::Result<frames::RawReceiverPayload>{frames::FrameError{
            .code = frames::FrameErrorCode::InvalidFrequencyAxis}};
    };

    expectFailure(
        std::move(source), validWorkItem(),
        SingleSweepFailureCode::RawSweepFailed,
        [](const OperationFailure& failure) {
            const auto* cause = std::get_if<frames::FrameError>(&failure.cause);
            ASSERT_NE(cause, nullptr);
            EXPECT_EQ(cause->code, frames::FrameErrorCode::InvalidFrequencyAxis);
        });
}

TEST(SingleSweepExecutorFailureTest, RawSourceExceptionIsPreserved) {
    RawSweepSource source = [](const frames::FrequencyAxis&,
                               std::stop_token)
        -> frames::Result<frames::RawReceiverPayload> {
        throw std::runtime_error{"source failed"};
    };

    expectFailure(
        std::move(source), validWorkItem(),
        SingleSweepFailureCode::RawSweepFailed,
        [](const OperationFailure& failure) {
            const auto* cause = std::get_if<std::exception_ptr>(&failure.cause);
            ASSERT_NE(cause, nullptr);
            EXPECT_THROW(std::rethrow_exception(*cause), std::runtime_error);
        });
}

TEST(SingleSweepExecutorFailureTest, RawFrameRejectionIsAtomic) {
    RawSweepSource source = [](const frames::FrequencyAxis& axis,
                               std::stop_token) {
        auto payload = simulation::simulateSweep(axis).value();
        payload.samples.pop_back();
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
                               std::stop_token) {
        auto payload = simulation::simulateSweep(axis).value();
        payload.samples.front().b1 = {.real = 0.0, .imaginary = 0.0};
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
            EXPECT_TRUE(std::holds_alternative<std::monostate>(failure.cause));
        });
}

TEST(SingleSweepExecutorFailureTest, PublishFailureKeepsExistingFrame) {
    OperationManager manager;
    TraceDisplayFrameRepository repository{1};
    const auto existing = repository.publish(TraceDisplayFrame{
        .frameId = frames::FrameId{40},
        .traceId = display_model::TraceId{9},
        .stateRevision = 6,
        .sequenceNumber = 1,
        .format = display_model::TraceFormat::LogMagnitude,
        .valueUnit = display_model::ScaleUnit::Decibel,
        .frequenciesHz = {1.0, 2.0},
        .values = {-1.0, -2.0},
    });
    ASSERT_TRUE(existing.hasValue());
    SingleSweepExecutor executor{
        1, simulationSource(), manager, repository};

    const auto submitted = std::get<OperationSnapshot>(
        executor.submit(validWorkItem()));
    const auto terminal = awaitTerminal(manager, submitted);

    const auto* failed = std::get_if<OperationFailed>(&terminal.state);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(
        failed->error.code,
        SingleSweepFailureCode::TraceDisplayPublishFailed);
    const auto* cause =
        std::get_if<TraceDisplayFrameError>(&failed->error.cause);
    ASSERT_NE(cause, nullptr);
    EXPECT_EQ(cause->code, TraceDisplayFrameErrorCode::CapacityExceeded);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
    EXPECT_EQ(repository.latest(display_model::TraceId{9}), existing.value());
}

}  // namespace
}  // namespace vna::application
