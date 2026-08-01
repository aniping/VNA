#include <gtest/gtest.h>

#include <vna/measurement/s11_synthesizer.hpp>

namespace vna::measurement {
namespace {

frames::RawReceiverFrame rawFrame(
    std::vector<frames::RawReceiverSample> samples) {
    return frames::RawReceiverFrame{
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
            .points = static_cast<std::uint32_t>(samples.size()),
        },
        .payload = {
            .portCount = 2,
            .sourceStates = {{
                .sourcePort = 1,
                .samples = std::move(samples),
            }},
        },
    };
}

domain::MeasurementSnapshot s11Measurement() {
    return domain::MeasurementSnapshot{
        .id = domain::MeasurementId{17},
        .channelId = domain::ChannelId{3},
        .type = domain::MeasurementType::S11,
    };
}

TEST(S11SynthesizerTest, ProducesComplexRatiosWithSourceCorrelation) {
    const auto result = synthesizeS11(
        rawFrame({
            {.reference = {1.0, 0.0},
             .responses = {{0.5, 0.25}, {0.01, 0.0}}},
            {.reference = {1.0, 1.0},
             .responses = {{0.0, 2.0}, {0.01, 0.0}}},
        }),
        s11Measurement());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().context.frameId, frames::FrameId{11});
    EXPECT_EQ(result.value().frequencyAxis.id, frames::FrequencyAxisId{13});
    EXPECT_EQ(result.value().measurementId, domain::MeasurementId{17});
    EXPECT_EQ(result.value().type, domain::MeasurementType::S11);
    ASSERT_EQ(result.value().samples.size(), 2U);
    EXPECT_DOUBLE_EQ(result.value().samples[0].real, 0.5);
    EXPECT_DOUBLE_EQ(result.value().samples[0].imaginary, 0.25);
    EXPECT_DOUBLE_EQ(result.value().samples[1].real, 1.0);
    EXPECT_DOUBLE_EQ(result.value().samples[1].imaginary, 1.0);
}

TEST(S11SynthesizerTest, RejectsZeroReferenceBeforeDivision) {
    const auto result = synthesizeS11(
        rawFrame({
            {.reference = {0.0, 0.0},
             .responses = {{0.5, 0.25}, {0.01, 0.0}}},
            {.reference = {1.0, 0.0},
             .responses = {{-0.5, 0.25}, {0.01, 0.0}}},
        }),
        s11Measurement());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, frames::FrameErrorCode::ZeroReference);
}

TEST(S11SynthesizerTest, RejectsMeasurementFromAnotherChannel) {
    auto measurement = s11Measurement();
    measurement.channelId = domain::ChannelId{4};

    const auto result = synthesizeS11(
        rawFrame({
            {.reference = {1.0, 0.0},
             .responses = {{0.5, 0.25}, {0.01, 0.0}}},
            {.reference = {1.0, 0.0},
             .responses = {{-0.5, 0.25}, {0.01, 0.0}}},
        }),
        measurement);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(
        result.error().code,
        frames::FrameErrorCode::MeasurementChannelMismatch);
}

TEST(S11SynthesizerTest, RejectsUnsupportedMeasurementType) {
    auto measurement = s11Measurement();
    measurement.type = domain::MeasurementType::S21;

    const auto result = synthesizeS11(
        rawFrame({
            {.reference = {1.0, 0.0},
             .responses = {{0.5, 0.25}, {0.01, 0.0}}},
            {.reference = {1.0, 0.0},
             .responses = {{-0.5, 0.25}, {0.01, 0.0}}},
        }),
        measurement);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(
        result.error().code,
        frames::FrameErrorCode::UnsupportedMeasurementType);
}

}  // namespace
}  // namespace vna::measurement
