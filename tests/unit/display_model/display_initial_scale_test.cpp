#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#include <vna/display_model/display_workspace.hpp>

namespace vna::display_model {
namespace {

TEST(DisplayInitialScaleTest, RejectsInvalidScaleWithoutConsumingTraceId) {
    DisplayWorkspace workspace;
    const auto window = workspace.createWindow();
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto maximum = std::numeric_limits<double>::max();
    const auto epsilon = std::numeric_limits<double>::epsilon();
    const auto aboveTen = std::nextafter(10.0, infinity);
    const std::array invalid{
        CartesianScaleSettings{0.0, 0.0, 9.0},
        CartesianScaleSettings{infinity, 0.0, 9.0},
        CartesianScaleSettings{10.0, infinity, 9.0},
        CartesianScaleSettings{10.0, 0.0, infinity},
        CartesianScaleSettings{10.0, 0.0, -epsilon},
        CartesianScaleSettings{10.0, 0.0, aboveTen},
        CartesianScaleSettings{maximum, 0.0, maximum},
    };

    for (const auto& settings : invalid) {
        const auto rejected = workspace.createTrace(
            window,
            domain::MeasurementId{1},
            TraceFormat::LogMagnitude,
            settings);
        ASSERT_FALSE(rejected.hasValue());
        EXPECT_EQ(
            rejected.error().code,
            DisplayErrorCode::InvalidScalePerDivision);
    }
    EXPECT_TRUE(workspace.snapshot().traces.empty());
    const auto created = workspace.createTrace(
        window,
        domain::MeasurementId{1},
        TraceFormat::LogMagnitude);
    ASSERT_TRUE(created.hasValue());
    EXPECT_EQ(created.value(), TraceId{1});
}

TEST(DisplayInitialScaleTest, DerivesRangeFromExplicitIndependentValues) {
    DisplayWorkspace workspace;
    const auto window = workspace.createWindow();

    const auto trace = workspace.createTrace(
        window,
        domain::MeasurementId{1},
        TraceFormat::LogMagnitude,
        CartesianScaleSettings{5.0, 3.0, 2.0});

    ASSERT_TRUE(trace.hasValue());
    const auto snapshot = workspace.snapshot();
    ASSERT_EQ(snapshot.traces.size(), 1U);
    ASSERT_TRUE(snapshot.traces[0].scale.has_value());
    const auto& scale = *snapshot.traces[0].scale;
    EXPECT_DOUBLE_EQ(scale.scalePerDivision, 5.0);
    EXPECT_DOUBLE_EQ(scale.referenceValue, 3.0);
    EXPECT_DOUBLE_EQ(scale.referencePosition, 2.0);
    EXPECT_DOUBLE_EQ(scale.minimum, -7.0);
    EXPECT_DOUBLE_EQ(scale.maximum, 43.0);
}

}  // namespace
}  // namespace vna::display_model
