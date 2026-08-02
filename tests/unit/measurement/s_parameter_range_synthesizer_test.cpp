#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <span>
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

SParameterRangeSynthesisRequest rangeRequest(
    std::span<const frames::RawReceiverSample> samples,
    std::span<const domain::MeasurementSnapshot> measurements) {
    return {
        .sourcePort = 2,
        .firstPoint = 4,
        .totalPointCount = 10,
        .portCount = 2,
        .samples = samples,
        .measurements = measurements,
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

    const auto result =
        synthesizeSParameterRanges(rangeRequest(samples, measurements));

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

    const auto result =
        synthesizeSParameterRanges(rangeRequest(samples, measurements));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::ZeroReference);
}

TEST(SParameterRangeSynthesizerTest, RejectsSourceOutsidePortCount) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S22)};

    auto request = rangeRequest(samples, measurements);
    request.sourcePort = 3;
    const auto result = synthesizeSParameterRanges(request);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::InvalidSourcePort);
}

TEST(SParameterRangeSynthesizerTest, ChecksResponsePortsOnlyForMatchingSource) {
    const std::array samples{
        frames::RawReceiverSample{{1.0, 0.0}, {{0.5, 0.0}}}};
    const std::array matching{
        measurement(31, domain::MeasurementType::S21)};
    const std::array otherSource{
        measurement(32, domain::MeasurementType::S22)};

    auto request = SParameterRangeSynthesisRequest{
        .sourcePort = 1,
        .firstPoint = 0,
        .totalPointCount = 2,
        .portCount = 1,
        .samples = samples,
        .measurements = matching,
    };
    const auto rejected = synthesizeSParameterRanges(request);

    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, frames::FrameErrorCode::InvalidPortCount);

    request.measurements = otherSource;
    const auto ignored = synthesizeSParameterRanges(request);
    ASSERT_TRUE(ignored.hasValue());
    EXPECT_TRUE(ignored.value().empty());
}

TEST(SParameterRangeSynthesizerTest, RejectsRangePastTotalPointCount) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S22)};

    auto request = rangeRequest(samples, measurements);
    request.firstPoint = 9;
    const auto result = synthesizeSParameterRanges(request);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::SampleCountMismatch);
}

TEST(SParameterRangeSynthesizerTest, RejectsMissingResponseReceiver) {
    auto samples = sourceTwoSamples();
    samples[0].responses.pop_back();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S12)};

    const auto result =
        synthesizeSParameterRanges(rangeRequest(samples, measurements));

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

    const auto result =
        synthesizeSParameterRanges(rangeRequest(samples, measurements));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::NonFiniteSample);
}

TEST(SParameterRangeSynthesizerTest, RejectsNonFiniteSynthesizedRatio) {
    auto samples = sourceTwoSamples();
    samples[0].reference = {std::numeric_limits<double>::min(), 0.0};
    samples[0].responses[0] = {std::numeric_limits<double>::max(), 0.0};
    const std::array measurements{
        measurement(31, domain::MeasurementType::S12)};

    const auto result =
        synthesizeSParameterRanges(rangeRequest(samples, measurements));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::NonFiniteSample);
}

TEST(SParameterRangeSynthesizerTest, InvalidMeasurementFailsWholeRangeBatch) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S22),
        measurement(0, domain::MeasurementType::S12),
    };

    const auto result =
        synthesizeSParameterRanges(rangeRequest(samples, measurements));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::InvalidMeasurementId);
}

TEST(SParameterRangeSynthesizerTest, EmptyApplicableSetSucceeds) {
    const auto samples = sourceTwoSamples();
    const std::array measurements{
        measurement(31, domain::MeasurementType::S11),
        measurement(32, domain::MeasurementType::S21),
    };

    const auto result =
        synthesizeSParameterRanges(rangeRequest(samples, measurements));

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}

}  // namespace
}  // namespace vna::measurement
