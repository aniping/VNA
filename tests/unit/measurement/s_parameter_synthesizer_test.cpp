#include <gtest/gtest.h>

#include <vna/measurement/s_parameter_synthesizer.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace vna::measurement {
namespace {

frames::RawReceiverFrame twoPortFrame(bool reverseSourceStates = false) {
    std::vector<frames::RawSourceState> states{
        {
            .sourcePort = 1,
            .samples = {
                {{1.0, 0.0}, {{2.0, 1.0}, {-1.0, 0.5}}},
                {{2.0, 0.0}, {{4.0, 2.0}, {-2.0, 1.0}}},
            },
        },
        {
            .sourcePort = 2,
            .samples = {
                {{0.0, 2.0}, {{-2.0, 1.0}, {-0.5, -2.0}}},
                {{1.0, -1.0}, {{1.5, 0.5}, {-0.75, 1.25}}},
            },
        },
    };
    if (reverseSourceStates) {
        std::reverse(states.begin(), states.end());
    }
    return {
        .context = {
            .frameId = frames::FrameId{11},
            .sweepId = frames::SweepId{7},
            .channelId = domain::ChannelId{3},
            .stateRevision = 19,
            .sequenceNumber = 5,
        },
        .frequencyAxis = {
            .id = frames::FrequencyAxisId{13},
            .startFrequencyHz = 1'000'000,
            .stopFrequencyHz = 2'000'000,
            .points = 2,
        },
        .payload = {.portCount = 2, .sourceStates = std::move(states)},
    };
}

domain::MeasurementSnapshot measurement(domain::MeasurementType type) {
    return {
        .id = domain::MeasurementId{17},
        .channelId = domain::ChannelId{3},
        .type = type,
    };
}

TEST(SParameterSynthesizerTest, SelectsAllTwoPortRatiosByMatrixIndex) {
    struct ExpectedRatio {
        domain::MeasurementType type;
        frames::ComplexSample value;
    };
    const std::array cases{
        ExpectedRatio{domain::MeasurementType::S11, {2.0, 1.0}},
        ExpectedRatio{domain::MeasurementType::S21, {-1.0, 0.5}},
        ExpectedRatio{domain::MeasurementType::S12, {0.5, 1.0}},
        ExpectedRatio{domain::MeasurementType::S22, {-1.0, 0.25}},
    };

    for (const auto& expected : cases) {
        SCOPED_TRACE(static_cast<int>(expected.type));
        const auto result =
            synthesizeSParameter(twoPortFrame(), measurement(expected.type));

        ASSERT_TRUE(result.hasValue());
        ASSERT_EQ(result.value().samples.size(), 2U);
        EXPECT_EQ(result.value().type, expected.type);
        EXPECT_DOUBLE_EQ(result.value().samples[0].real, expected.value.real);
        EXPECT_DOUBLE_EQ(
            result.value().samples[0].imaginary,
            expected.value.imaginary);
    }
}

TEST(SParameterSynthesizerTest, SourceStateOrderDoesNotSelectTheWrongPort) {
    const std::array types{
        domain::MeasurementType::S11,
        domain::MeasurementType::S21,
        domain::MeasurementType::S12,
        domain::MeasurementType::S22,
    };

    for (const auto type : types) {
        const auto ordered =
            synthesizeSParameter(twoPortFrame(), measurement(type));
        const auto reversed =
            synthesizeSParameter(twoPortFrame(true), measurement(type));

        ASSERT_TRUE(ordered.hasValue());
        ASSERT_TRUE(reversed.hasValue());
        EXPECT_EQ(reversed.value().samples, ordered.value().samples);
    }
}

TEST(SParameterSynthesizerTest, RejectsMissingSecondSourceState) {
    auto input = twoPortFrame();
    input.payload.sourceStates.pop_back();

    const auto result = synthesizeSParameter(
        std::move(input),
        measurement(domain::MeasurementType::S12));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::InvalidSourcePort);
}

TEST(SParameterSynthesizerTest, RejectsMissingSecondResponse) {
    auto input = twoPortFrame();
    input.payload.sourceStates.front().samples.front().responses.pop_back();

    const auto result = synthesizeSParameter(
        std::move(input),
        measurement(domain::MeasurementType::S21));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(
        result.error().code,
        frames::FrameErrorCode::ResponseCountMismatch);
}

TEST(SParameterSynthesizerTest, RejectsZeroReferenceInSelectedSourceState) {
    auto input = twoPortFrame();
    input.payload.sourceStates.back().samples.front().reference = {0.0, 0.0};

    const auto result = synthesizeSParameter(
        std::move(input),
        measurement(domain::MeasurementType::S22));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::ZeroReference);
}

TEST(SParameterSynthesizerTest, RejectsNonFiniteReceiverData) {
    auto input = twoPortFrame();
    input.payload.sourceStates.back().samples.front().responses.front().real =
        std::numeric_limits<double>::infinity();

    const auto result = synthesizeSParameter(
        std::move(input),
        measurement(domain::MeasurementType::S12));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::NonFiniteSample);
}

TEST(SParameterBatchSynthesizerTest, EmptyRequestProducesAnEmptyBatch) {
    const std::vector<domain::MeasurementSnapshot> measurements;

    const auto result = synthesizeSParameters(twoPortFrame(), measurements);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}

TEST(SParameterBatchSynthesizerTest, ReusesOneRatioForDifferentMeasurementIds) {
    auto first = measurement(domain::MeasurementType::S12);
    auto second = first;
    first.id = domain::MeasurementId{21};
    second.id = domain::MeasurementId{22};
    const std::array measurements{first, second};

    const auto result = synthesizeSParameters(twoPortFrame(), measurements);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 2U);
    EXPECT_EQ(result.value()[0].measurementId, domain::MeasurementId{21});
    EXPECT_EQ(result.value()[1].measurementId, domain::MeasurementId{22});
    EXPECT_EQ(result.value()[0].samples, result.value()[1].samples);
}

TEST(SParameterBatchSynthesizerTest, PreservesRequestedFourTypeOrder) {
    const std::array measurements{
        measurement(domain::MeasurementType::S22),
        measurement(domain::MeasurementType::S11),
        measurement(domain::MeasurementType::S12),
        measurement(domain::MeasurementType::S21),
    };

    const auto result = synthesizeSParameters(twoPortFrame(), measurements);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), measurements.size());
    for (std::size_t index = 0; index < measurements.size(); ++index) {
        EXPECT_EQ(result.value()[index].type, measurements[index].type);
    }
}

TEST(SParameterBatchSynthesizerTest, AnyInvalidMeasurementFailsTheWholeBatch) {
    auto wrongChannel = measurement(domain::MeasurementType::S12);
    wrongChannel.channelId = domain::ChannelId{99};
    const std::array measurements{
        measurement(domain::MeasurementType::S11),
        wrongChannel,
        measurement(domain::MeasurementType::S21),
    };

    const auto result = synthesizeSParameters(twoPortFrame(), measurements);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(
        result.error().code,
        frames::FrameErrorCode::MeasurementChannelMismatch);
}

}  // namespace
}  // namespace vna::measurement
