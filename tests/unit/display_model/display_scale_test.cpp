#include <gtest/gtest.h>

#include <limits>

#include <vna/display_model/display_workspace.hpp>

namespace vna::display_model {
namespace {

TEST(DisplayScaleTest, CreatesLogMagnitudeWithOfficialDefaultScale) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();
    const auto trace = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);
    ASSERT_TRUE(trace.hasValue());

    const auto snapshot = workspace.snapshot();
    ASSERT_EQ(snapshot.traces.size(), 1U);
    ASSERT_TRUE(snapshot.traces[0].scale.has_value());
    const auto& scale = snapshot.traces[0].scale.value();
    EXPECT_DOUBLE_EQ(scale.scalePerDivision, 10.0);
    EXPECT_DOUBLE_EQ(scale.referenceValue, 0.0);
    EXPECT_DOUBLE_EQ(scale.referencePosition, 8.0);
    EXPECT_DOUBLE_EQ(scale.minimum, -80.0);
    EXPECT_DOUBLE_EQ(scale.maximum, 20.0);
    EXPECT_EQ(scale.unit, ScaleUnit::Decibel);
}

TEST(DisplayScaleTest, UpdatesScalePerDivisionAndDerivedRange) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();
    const auto trace = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);
    ASSERT_TRUE(trace.hasValue());

    const auto updated =
        workspace.updateTraceScalePerDivision(trace.value(), 5.0);

    ASSERT_TRUE(updated.hasValue());
    EXPECT_EQ(updated.value(), trace.value());
    const auto snapshot = workspace.snapshot();
    const auto& scale = snapshot.traces[0].scale.value();
    EXPECT_DOUBLE_EQ(scale.scalePerDivision, 5.0);
    EXPECT_DOUBLE_EQ(scale.referenceValue, 0.0);
    EXPECT_DOUBLE_EQ(scale.referencePosition, 8.0);
    EXPECT_DOUBLE_EQ(scale.minimum, -40.0);
    EXPECT_DOUBLE_EQ(scale.maximum, 10.0);
    EXPECT_EQ(scale.unit, ScaleUnit::Decibel);
}

class InvalidScalePerDivisionTest
    : public ::testing::TestWithParam<double> {};

TEST_P(
    InvalidScalePerDivisionTest,
    RejectsInvalidAndOverflowingScaleWithoutChangingState) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();
    const auto trace = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);
    ASSERT_TRUE(trace.hasValue());

    const auto updated =
        workspace.updateTraceScalePerDivision(trace.value(), GetParam());

    ASSERT_FALSE(updated.hasValue());
    EXPECT_EQ(
        updated.error().code,
        DisplayErrorCode::InvalidScalePerDivision);
    const auto snapshot = workspace.snapshot();
    const auto& scale = snapshot.traces[0].scale.value();
    EXPECT_DOUBLE_EQ(scale.scalePerDivision, 10.0);
    EXPECT_DOUBLE_EQ(scale.minimum, -80.0);
    EXPECT_DOUBLE_EQ(scale.maximum, 20.0);
}

INSTANTIATE_TEST_SUITE_P(
    InvalidValues,
    InvalidScalePerDivisionTest,
    ::testing::Values(
        0.0,
        -1.0,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::max()));

TEST(DisplayScaleTest, AppliesScalePolicyAcrossFormatTransitions) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();
    const auto trace = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);
    ASSERT_TRUE(trace.hasValue());
    ASSERT_TRUE(
        workspace.updateTraceScalePerDivision(trace.value(), 5.0).hasValue());

    ASSERT_TRUE(workspace.updateTraceFormat(
        trace.value(), TraceFormat::LogMagnitude).hasValue());
    EXPECT_DOUBLE_EQ(
        workspace.snapshot().traces[0].scale->scalePerDivision,
        5.0);

    ASSERT_TRUE(workspace.updateTraceFormat(
        trace.value(), TraceFormat::Phase).hasValue());
    EXPECT_FALSE(workspace.snapshot().traces[0].scale.has_value());
    const auto phaseUpdate =
        workspace.updateTraceScalePerDivision(trace.value(), 2.0);
    ASSERT_FALSE(phaseUpdate.hasValue());
    EXPECT_EQ(
        phaseUpdate.error().code,
        DisplayErrorCode::ScaleNotSupportedForFormat);

    ASSERT_TRUE(workspace.updateTraceFormat(
        trace.value(), TraceFormat::Smith).hasValue());
    EXPECT_FALSE(workspace.snapshot().traces[0].scale.has_value());
    const auto smithUpdate =
        workspace.updateTraceScalePerDivision(trace.value(), 2.0);
    ASSERT_FALSE(smithUpdate.hasValue());
    EXPECT_EQ(
        smithUpdate.error().code,
        DisplayErrorCode::ScaleNotSupportedForFormat);

    ASSERT_TRUE(workspace.updateTraceFormat(
        trace.value(), TraceFormat::LogMagnitude).hasValue());
    const auto snapshot = workspace.snapshot();
    const auto& restored = snapshot.traces[0].scale.value();
    EXPECT_DOUBLE_EQ(restored.scalePerDivision, 10.0);
    EXPECT_DOUBLE_EQ(restored.minimum, -80.0);
    EXPECT_DOUBLE_EQ(restored.maximum, 20.0);
}

class UnsupportedScaleFormatTest
    : public ::testing::TestWithParam<TraceFormat> {};

TEST_P(
    UnsupportedScaleFormatTest,
    CreatesTraceWithoutModeledScaleAndRejectsUpdate) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();
    const auto trace = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        GetParam());
    ASSERT_TRUE(trace.hasValue());

    EXPECT_FALSE(workspace.snapshot().traces[0].scale.has_value());
    const auto updated =
        workspace.updateTraceScalePerDivision(trace.value(), 2.0);
    ASSERT_FALSE(updated.hasValue());
    EXPECT_EQ(
        updated.error().code,
        DisplayErrorCode::ScaleNotSupportedForFormat);
    EXPECT_FALSE(workspace.snapshot().traces[0].scale.has_value());
}

INSTANTIATE_TEST_SUITE_P(
    UnsupportedFormats,
    UnsupportedScaleFormatTest,
    ::testing::Values(TraceFormat::Phase, TraceFormat::Smith));

TEST(DisplayScaleTest, UpdatesOnlySelectedTraceScale) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();
    const auto first = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);
    const auto second = workspace.createTrace(
        windowId,
        domain::MeasurementId{8},
        TraceFormat::LogMagnitude);
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());

    ASSERT_TRUE(
        workspace.updateTraceScalePerDivision(first.value(), 5.0).hasValue());

    const auto snapshot = workspace.snapshot();
    ASSERT_EQ(snapshot.traces.size(), 2U);
    EXPECT_DOUBLE_EQ(snapshot.traces[0].scale->scalePerDivision, 5.0);
    EXPECT_DOUBLE_EQ(snapshot.traces[1].scale->scalePerDivision, 10.0);
}

TEST(DisplayScaleTest, RejectsScaleUpdateForMissingTrace) {
    DisplayWorkspace workspace;

    const auto updated =
        workspace.updateTraceScalePerDivision(TraceId{42}, 5.0);

    ASSERT_FALSE(updated.hasValue());
    EXPECT_EQ(updated.error().code, DisplayErrorCode::TraceNotFound);
    EXPECT_TRUE(workspace.snapshot().traces.empty());
}

}  // namespace
}  // namespace vna::display_model
