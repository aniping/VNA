#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include <vna/frames/frames.hpp>

namespace vna::frames {
namespace {

FrameContext validContext() {
    return FrameContext{
        .frameId = FrameId{11},
        .sweepId = SweepId{7},
        .channelId = domain::ChannelId{3},
        .stateRevision = 19,
        .sequenceNumber = 5,
    };
}

FrequencyAxis validAxis(std::uint32_t points = 2) {
    return FrequencyAxis{
        .id = FrequencyAxisId{13},
        .startFrequencyHz = 1'000'000,
        .stopFrequencyHz = 2'000'000,
        .points = points,
    };
}

RawReceiverPayload validRawPayload(std::uint32_t points = 2) {
    return RawReceiverPayload{
        .samples = std::vector<RawReceiverSample>(
            points,
            {.a1 = {1.0, 0.0}, .b1 = {0.5, 0.25}}),
    };
}

TEST(FrameContractTest, CreatesRawReceiverFrameFromCoordinatorContextAndPayload) {
    const auto context = validContext();
    const auto axis = validAxis();
    RawReceiverPayload payload{
        .samples = {
            {.a1 = {1.0, 0.0}, .b1 = {0.5, 0.0}},
            {.a1 = {1.0, 0.0}, .b1 = {-0.5, 0.25}},
        },
    };

    const auto result =
        makeRawReceiverFrame(context, axis, std::move(payload));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().context.frameId, FrameId{11});
    EXPECT_EQ(result.value().context.stateRevision, 19U);
    EXPECT_EQ(result.value().frequencyAxis.points, 2U);
    ASSERT_EQ(result.value().payload.samples.size(), 2U);
    EXPECT_DOUBLE_EQ(result.value().payload.samples[1].b1.real, -0.5);
    EXPECT_DOUBLE_EQ(result.value().payload.samples[1].b1.imaginary, 0.25);
}

struct InvalidContextCase {
    const char* name;
    FrameContext context;
};

class InvalidContextTest
    : public ::testing::TestWithParam<InvalidContextCase> {};

TEST_P(InvalidContextTest, RejectsIncompleteCoordinatorContext) {
    const auto result = makeRawReceiverFrame(
        GetParam().context, validAxis(), validRawPayload());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, FrameErrorCode::InvalidFrameContext);
}

std::vector<InvalidContextCase> invalidContexts() {
    auto missingFrame = validContext();
    missingFrame.frameId = FrameId{0};
    auto missingSweep = validContext();
    missingSweep.sweepId = SweepId{0};
    auto missingChannel = validContext();
    missingChannel.channelId = domain::ChannelId{0};
    auto missingSequence = validContext();
    missingSequence.sequenceNumber = 0;
    return {
        {"MissingFrame", missingFrame},
        {"MissingSweep", missingSweep},
        {"MissingChannel", missingChannel},
        {"MissingSequence", missingSequence},
    };
}

INSTANTIATE_TEST_SUITE_P(
    MissingIdentity,
    InvalidContextTest,
    ::testing::ValuesIn(invalidContexts()),
    [](const auto& info) { return info.param.name; });

struct InvalidAxisCase {
    const char* name;
    FrequencyAxis axis;
    FrameErrorCode expected;
};

class InvalidAxisTest : public ::testing::TestWithParam<InvalidAxisCase> {};

TEST_P(InvalidAxisTest, RejectsInvalidOrOversizedFrequencyAxis) {
    const auto result = makeRawReceiverFrame(
        validContext(), GetParam().axis, validRawPayload());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, GetParam().expected);
}

std::vector<InvalidAxisCase> invalidAxes() {
    auto missingId = validAxis();
    missingId.id = FrequencyAxisId{0};
    auto reversed = validAxis();
    reversed.startFrequencyHz = reversed.stopFrequencyHz;
    auto tooShort = validAxis(1);
    auto tooLong = validAxis(kMaxSweepPoints + 1);
    return {
        {"MissingId", missingId, FrameErrorCode::InvalidFrequencyAxis},
        {"Reversed", reversed, FrameErrorCode::InvalidFrequencyAxis},
        {"TooShort", tooShort, FrameErrorCode::InvalidFrequencyAxis},
        {"TooLong", tooLong, FrameErrorCode::PointCountExceeded},
    };
}

INSTANTIATE_TEST_SUITE_P(
    InvalidBoundary,
    InvalidAxisTest,
    ::testing::ValuesIn(invalidAxes()),
    [](const auto& info) { return info.param.name; });

TEST(FrameContractTest, AcceptsMaximumSweepPointCount) {
    const auto result = makeRawReceiverFrame(
        validContext(),
        validAxis(kMaxSweepPoints),
        validRawPayload(kMaxSweepPoints));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().frequencyAxis.points, 2048U);
    EXPECT_EQ(result.value().payload.samples.size(), 2048U);
}

TEST(FrameContractTest, RejectsRawReceiverSampleCountMismatch) {
    const auto result = makeRawReceiverFrame(
        validContext(), validAxis(3), validRawPayload(2));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, FrameErrorCode::SampleCountMismatch);
}

TEST(FrameContractTest, RejectsNonFiniteRawReceiverSample) {
    auto payload = validRawPayload();
    payload.samples[1].b1.imaginary =
        std::numeric_limits<double>::infinity();

    const auto result = makeRawReceiverFrame(
        validContext(), validAxis(), std::move(payload));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, FrameErrorCode::NonFiniteSample);
}

TEST(FrameContractTest, CreatesS11MeasurementFrameWithSharedCorrelation) {
    std::vector<ComplexSample> samples{{0.5, 0.0}, {-0.5, 0.25}};

    const auto result = makeMeasurementFrame(
        validContext(),
        validAxis(),
        domain::MeasurementId{17},
        domain::MeasurementType::S11,
        std::move(samples));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().context.frameId, FrameId{11});
    EXPECT_EQ(result.value().frequencyAxis.id, FrequencyAxisId{13});
    EXPECT_EQ(result.value().measurementId, domain::MeasurementId{17});
    EXPECT_EQ(result.value().type, domain::MeasurementType::S11);
    ASSERT_EQ(result.value().samples.size(), 2U);
    EXPECT_DOUBLE_EQ(result.value().samples[1].real, -0.5);
    EXPECT_DOUBLE_EQ(result.value().samples[1].imaginary, 0.25);
}

struct InvalidMeasurementCase {
    const char* name;
    domain::MeasurementId id;
    domain::MeasurementType type;
    std::vector<ComplexSample> samples;
    FrameErrorCode expected;
};

class InvalidMeasurementTest
    : public ::testing::TestWithParam<InvalidMeasurementCase> {};

TEST_P(InvalidMeasurementTest, RejectsUnsupportedOrMalformedMeasurement) {
    const auto& parameter = GetParam();
    const auto result = makeMeasurementFrame(
        validContext(),
        validAxis(),
        parameter.id,
        parameter.type,
        parameter.samples);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, parameter.expected);
}

std::vector<InvalidMeasurementCase> invalidMeasurements() {
    const auto infinity = std::numeric_limits<double>::infinity();
    return {
        {"MissingId", domain::MeasurementId{0}, domain::MeasurementType::S11,
         {{0.5, 0.0}, {-0.5, 0.25}}, FrameErrorCode::InvalidMeasurementId},
        {"S21", domain::MeasurementId{17}, domain::MeasurementType::S21,
         {{0.5, 0.0}, {-0.5, 0.25}},
         FrameErrorCode::UnsupportedMeasurementType},
        {"WrongCount", domain::MeasurementId{17}, domain::MeasurementType::S11,
         {{0.5, 0.0}}, FrameErrorCode::SampleCountMismatch},
        {"NonFinite", domain::MeasurementId{17}, domain::MeasurementType::S11,
         {{0.5, 0.0}, {-0.5, infinity}}, FrameErrorCode::NonFiniteSample},
    };
}

INSTANTIATE_TEST_SUITE_P(
    InvalidBoundary,
    InvalidMeasurementTest,
    ::testing::ValuesIn(invalidMeasurements()),
    [](const auto& info) { return info.param.name; });

}  // namespace
}  // namespace vna::frames
