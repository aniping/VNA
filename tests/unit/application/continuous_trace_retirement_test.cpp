#include <gtest/gtest.h>

#include <variant>

#include <vna/application/disabled_single_sweep_execution.hpp>
#include <vna/application/trace_display_frame_repository.hpp>

#include "single_sweep_executor_test_support.hpp"

namespace vna::application {
namespace {

TraceDisplayFrame seedFrame() {
    return {
        .frameId = frames::FrameId{91},
        .traceId = display_model::TraceId{1},
        .measurementId = domain::MeasurementId{1},
        .measurementType = domain::MeasurementType::S21,
        .stateRevision = 6,
        .generation = 1,
        .sequenceNumber = 9,
        .format = display_model::TraceFormat::LogMagnitude,
        .frequenciesHz = {1'000'000, 2'000'000},
        .samples = CartesianTraceDisplaySamples{
            .unit = TraceDisplayUnit::Decibel,
            .values = {-3.0, -2.0}},
    };
}

TEST(DisabledSingleSweepExecutionTest, RejectsWithoutOwningDisplayState) {
    TraceDisplayFrameRepository repository{1};
    ASSERT_TRUE(repository.publish(seedFrame()).hasValue());
    const auto retained = repository.latest(display_model::TraceId{1});
    DisabledSingleSweepExecution disabled;

    const auto result = disabled.submit(test_support::validWorkItem());
    disabled.invalidateTraceFrame(display_model::TraceId{1});
    disabled.discardTrace(display_model::TraceId{1});

    const auto* error = std::get_if<SingleSweepSubmitError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, SingleSweepSubmitErrorCode::Stopped);
    EXPECT_EQ(repository.latest(display_model::TraceId{1}), retained);
}

}  // namespace
}  // namespace vna::application
