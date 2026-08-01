#include <gtest/gtest.h>
#include <limits>
#include <utility>
#include <variant>
#include <vna/application/trace_display_frame_repository.hpp>
namespace vna::application {
namespace {

TraceDisplayFrame cartesianFrame(
    display_model::TraceFormat format,
    TraceDisplayUnit unit) {
    return {
        .frameId = frames::FrameId{7},
        .traceId = display_model::TraceId{3},
        .measurementId = domain::MeasurementId{5},
        .measurementType = domain::MeasurementType::S21,
        .stateRevision = 11,
        .generation = 17,
        .sequenceNumber = 13,
        .format = format,
        .frequenciesHz = {1.0e6, 2.0e6},
        .samples = CartesianTraceDisplaySamples{
            .unit = unit,
            .values = {-6.0, 45.0}},
    };
}
TraceDisplayFrame smithFrame() {
    return {
        .frameId = frames::FrameId{8},
        .traceId = display_model::TraceId{4},
        .measurementId = domain::MeasurementId{6},
        .measurementType = domain::MeasurementType::S21,
        .stateRevision = 12,
        .generation = 18,
        .sequenceNumber = 14,
        .format = display_model::TraceFormat::Smith,
        .frequenciesHz = {1.0e6, 2.0e6},
        .samples = ComplexTraceDisplaySamples{
            .unit = TraceDisplayUnit::Unitless,
            // Smith displays the synthesized Sij itself. Values outside the
            // unit circle remain valid because this boundary is not a clipper.
            .values = {{1.5, -2.0}, {-0.25, 0.75}}},
    };
}
void expectError(
    TraceDisplayFrameRepository& repository,
    TraceDisplayFrame frame,
    TraceDisplayFrameErrorCode expected) {
    const auto result = repository.publish(std::move(frame));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, expected);
}
TEST(TraceDisplayFrameFormatsTest, PublishesEachSupportedPresentation) {
    TraceDisplayFrameRepository repository{3};
    auto log = cartesianFrame(
        display_model::TraceFormat::LogMagnitude,
        TraceDisplayUnit::Decibel);
    auto phase = cartesianFrame(
        display_model::TraceFormat::Phase,
        TraceDisplayUnit::Degree);
    phase.traceId = display_model::TraceId{4};
    phase.frameId = frames::FrameId{8};
    auto smith = smithFrame();
    smith.traceId = display_model::TraceId{5};
    smith.frameId = frames::FrameId{9};
    ASSERT_TRUE(repository.publish(std::move(log)).hasValue());
    ASSERT_TRUE(repository.publish(std::move(phase)).hasValue());
    const auto publishedSmith = repository.publish(std::move(smith));
    ASSERT_TRUE(publishedSmith.hasValue());
    const auto* samples = std::get_if<ComplexTraceDisplaySamples>(
        &publishedSmith.value()->samples);
    ASSERT_NE(samples, nullptr);
    EXPECT_EQ(samples->unit, TraceDisplayUnit::Unitless);
    EXPECT_EQ(samples->values[0], (frames::ComplexSample{1.5, -2.0}));
}

TEST(TraceDisplayFrameFormatsTest, RejectsFormatPayloadAndUnitMismatches) {
    TraceDisplayFrameRepository repository{1};
    auto phaseWithDecibels = cartesianFrame(
        display_model::TraceFormat::Phase,
        TraceDisplayUnit::Decibel);
    expectError(
        repository,
        std::move(phaseWithDecibels),
        TraceDisplayFrameErrorCode::UnsupportedValueUnit);
    auto smithWithCartesian = cartesianFrame(
        display_model::TraceFormat::Smith,
        TraceDisplayUnit::Degree);
    expectError(
        repository,
        std::move(smithWithCartesian),
        TraceDisplayFrameErrorCode::SamplePayloadMismatch);
    auto logWithComplex = smithFrame();
    logWithComplex.format = display_model::TraceFormat::LogMagnitude;
    expectError(
        repository,
        std::move(logWithComplex),
        TraceDisplayFrameErrorCode::SamplePayloadMismatch);
}

TEST(TraceDisplayFrameFormatsTest, RejectsMissingMeasurementAndComplexNaN) {
    TraceDisplayFrameRepository repository{1};
    auto missingMeasurement = smithFrame();
    missingMeasurement.measurementId = domain::MeasurementId{0};
    expectError(
        repository,
        std::move(missingMeasurement),
        TraceDisplayFrameErrorCode::InvalidMeasurementId);
    auto invalidType = smithFrame();
    invalidType.measurementType = static_cast<domain::MeasurementType>(99);
    expectError(
        repository,
        std::move(invalidType),
        TraceDisplayFrameErrorCode::InvalidMeasurementType);
    auto missingGeneration = smithFrame();
    missingGeneration.generation = 0;
    expectError(
        repository,
        std::move(missingGeneration),
        TraceDisplayFrameErrorCode::InvalidGeneration);
    auto nonFinite = smithFrame();
    std::get<ComplexTraceDisplaySamples>(nonFinite.samples)
        .values[1]
        .imaginary = std::numeric_limits<double>::quiet_NaN();
    expectError(
        repository,
        std::move(nonFinite),
        TraceDisplayFrameErrorCode::NonFiniteValue);
}

TEST(TraceDisplayFrameFormatsTest, RejectionPreservesLastGoodFrame) {
    TraceDisplayFrameRepository repository{1};
    const auto good = repository.publish(smithFrame());
    ASSERT_TRUE(good.hasValue());
    auto mismatched = smithFrame();
    mismatched.frameId = frames::FrameId{9};
    mismatched.sequenceNumber = 15;
    std::get<ComplexTraceDisplaySamples>(mismatched.samples).values.pop_back();

    expectError(
        repository,
        std::move(mismatched),
        TraceDisplayFrameErrorCode::SampleCountMismatch);

    EXPECT_EQ(repository.latest(display_model::TraceId{4}), good.value());
}

}  // namespace
}  // namespace vna::application
