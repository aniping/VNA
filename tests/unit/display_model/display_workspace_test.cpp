#include <gtest/gtest.h>

#include <vna/display_model/display_workspace.hpp>

namespace vna::display_model {
namespace {

TEST(DisplayWorkspaceTest, CreatesWindowInWorkspace) {
    DisplayWorkspace workspace;

    const auto windowId = workspace.createWindow();

    EXPECT_EQ(windowId, WindowId{1});
    const auto snapshot = workspace.snapshot();
    ASSERT_EQ(snapshot.windows.size(), 1U);
    EXPECT_EQ(snapshot.windows[0].id, windowId);
}

TEST(DisplayWorkspaceTest, CreatesTraceForMeasurementReference) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();

    const auto trace = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);

    ASSERT_TRUE(trace.hasValue());
    EXPECT_EQ(trace.value(), TraceId{1});
    const auto snapshot = workspace.snapshot();
    ASSERT_EQ(snapshot.traces.size(), 1U);
    EXPECT_EQ(snapshot.traces[0].windowId, windowId);
    EXPECT_EQ(snapshot.traces[0].measurementId, domain::MeasurementId{7});
    EXPECT_EQ(snapshot.traces[0].format, TraceFormat::LogMagnitude);
}

TEST(DisplayWorkspaceTest, RejectsTraceForMissingWindow) {
    DisplayWorkspace workspace;

    const auto trace = workspace.createTrace(
        WindowId{42},
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);

    ASSERT_FALSE(trace.hasValue());
    EXPECT_EQ(trace.error().code, DisplayErrorCode::WindowNotFound);
    EXPECT_TRUE(workspace.snapshot().traces.empty());
}

TEST(DisplayWorkspaceTest, UpdatesTraceFormat) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();
    const auto trace = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);
    ASSERT_TRUE(trace.hasValue());

    const auto updated = workspace.updateTraceFormat(
        trace.value(),
        TraceFormat::Phase);

    ASSERT_TRUE(updated.hasValue());
    EXPECT_EQ(updated.value(), trace.value());
    ASSERT_EQ(workspace.snapshot().traces.size(), 1U);
    EXPECT_EQ(workspace.snapshot().traces[0].format, TraceFormat::Phase);
}

TEST(DisplayWorkspaceTest, RemovesOnlyTargetTrace) {
    DisplayWorkspace workspace;
    const auto windowId = workspace.createWindow();
    const auto first = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::LogMagnitude);
    const auto second = workspace.createTrace(
        windowId,
        domain::MeasurementId{7},
        TraceFormat::Phase);
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());

    const auto removed = workspace.removeTrace(first.value());

    ASSERT_TRUE(removed.hasValue());
    EXPECT_EQ(removed.value(), first.value());
    const auto snapshot = workspace.snapshot();
    ASSERT_EQ(snapshot.traces.size(), 1U);
    EXPECT_EQ(snapshot.traces[0].id, second.value());
    EXPECT_EQ(snapshot.traces[0].measurementId, domain::MeasurementId{7});
}

TEST(DisplayWorkspaceTest, RejectsFormatUpdateForMissingTrace) {
    DisplayWorkspace workspace;

    const auto updated = workspace.updateTraceFormat(
        TraceId{42},
        TraceFormat::Phase);

    ASSERT_FALSE(updated.hasValue());
    EXPECT_EQ(updated.error().code, DisplayErrorCode::TraceNotFound);
    EXPECT_TRUE(workspace.snapshot().traces.empty());
}

TEST(DisplayWorkspaceTest, RejectsRemovalForMissingTrace) {
    DisplayWorkspace workspace;

    const auto removed = workspace.removeTrace(TraceId{42});

    ASSERT_FALSE(removed.hasValue());
    EXPECT_EQ(removed.error().code, DisplayErrorCode::TraceNotFound);
    EXPECT_TRUE(workspace.snapshot().traces.empty());
}

}  // namespace
}  // namespace vna::display_model
