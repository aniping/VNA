#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <vector>

#include <vna/measurement/s_parameter_synthesizer.hpp>

namespace vna::measurement {
namespace {

domain::MeasurementSnapshot measurement(
    std::uint64_t id,
    domain::MeasurementType type) {
    return {
        .id = domain::MeasurementId{id},
        .channelId = domain::ChannelId{3},
        .type = type,
    };
}

std::vector<frames::RawReceiverSample> sourceTwoSamples() {
    return {
        {{0.0, 2.0}, {{-2.0, 1.0}, {-0.5, -2.0}}},
        {{1.0, -1.0}, {{1.5, 0.5}, {-0.75, 1.25}}},
    };
}

TEST(SParameterRangeSynthesizerTest,
     SelectsApplicableMeasurementsInInputOrder) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S22),
        measurement(32, domain::MeasurementType::S11),
        measurement(33, domain::MeasurementType::S12),
        measurement(34, domain::MeasurementType::S12),
    };

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 2,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 3U);
    EXPECT_EQ(result.value()[0].measurementId, domain::MeasurementId{31});
    EXPECT_EQ(result.value()[1].measurementId, domain::MeasurementId{33});
    EXPECT_EQ(result.value()[2].measurementId, domain::MeasurementId{34});
    for (const auto& range : result.value()) {
        EXPECT_EQ(range.firstPoint, 4U);
    }
    const std::vector<frames::ComplexSample> expectedS22{
        {-1.0, 0.25}, {-1.0, 0.25}};
    const std::vector<frames::ComplexSample> expectedS12{
        {0.5, 1.0}, {0.5, 1.0}};
    EXPECT_EQ(result.value()[0].samples, expectedS22);
    EXPECT_EQ(result.value()[1].samples, expectedS12);
    EXPECT_EQ(result.value()[2].samples, expectedS12);
}

TEST(SParameterRangeSynthesizerTest, RejectsZeroReferenceAtomically) {
    auto samples = sourceTwoSamples();
    samples[1].reference = {0.0, 0.0};
    const std::array measurements{
        measurement(31, domain::MeasurementType::S22),
        measurement(33, domain::MeasurementType::S12),
    };

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 2,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::ZeroReference);
}

TEST(SParameterRangeSynthesizerTest, RejectsSourceOutsidePortCount) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S22)};

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 3,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::InvalidSourcePort);
}

TEST(SParameterRangeSynthesizerTest, RejectsRangePastTotalPointCount) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S22)};

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 2,
        .firstPoint = 9,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::SampleCountMismatch);
}

TEST(SParameterRangeSynthesizerTest, RejectsMissingResponseReceiver) {
    auto samples = sourceTwoSamples();
    samples[0].responses.pop_back();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S12)};

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 2,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(
        result.error().code,
        frames::FrameErrorCode::ResponseCountMismatch);
}

TEST(SParameterRangeSynthesizerTest, RejectsNonFiniteReceiverSample) {
    auto samples = sourceTwoSamples();
    samples[0].responses[0].real = std::numeric_limits<double>::infinity();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S12)};

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 2,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::NonFiniteSample);
}

TEST(SParameterRangeSynthesizerTest, RejectsNonFiniteSynthesizedRatio) {
    auto samples = sourceTwoSamples();
    samples[0].reference = {std::numeric_limits<double>::min(), 0.0};
    samples[0].responses[0] = {std::numeric_limits<double>::max(), 0.0};
    const std::array measurements{
        measurement(31, domain::MeasurementType::S12)};

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 2,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::NonFiniteSample);
}

TEST(SParameterRangeSynthesizerTest, InvalidMeasurementFailsWholeRangeBatch) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S22),
        measurement(0, domain::MeasurementType::S12),
    };

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 2,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::InvalidMeasurementId);
}

TEST(SParameterRangeSynthesizerTest, EmptyApplicableSetSucceeds) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S11),
        measurement(32, domain::MeasurementType::S21),
    };

    const auto result = synthesizeSParameterRanges({
        .sourcePort = 2,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
    });

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}

}  // namespace
}  // namespace vna::measurement
