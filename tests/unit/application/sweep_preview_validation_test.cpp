#include <gtest/gtest.h>

#include <limits>
#include <utility>
#include <variant>

#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/test/sweep_status_test_support.hpp>

namespace vna::application {
namespace {

SweepPreview validPreview() {
    return {
        .identity = {1, acquisition::SweepId{9}},
        .channelId = domain::ChannelId{1},
        .stateRevision = 5,
        .sequenceNumber = 9,
        .totalPointCount = 4,
        .traces = {{
            .traceId = display_model::TraceId{1},
            .measurementId = domain::MeasurementId{1},
            .measurementType = domain::MeasurementType::S21,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = {1.0e6, 2.0e6},
            .samples = CartesianTraceDisplaySamples{
                .unit = TraceDisplayUnit::Decibel,
                .values = {-1.0, -2.0}},
        }},
    };
}

SweepPreviewErrorCode rejectedCode(SweepPreview preview) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    const auto result = exchange.publish(std::move(preview));
    EXPECT_TRUE(std::holds_alternative<SweepPreviewError>(result));
    return std::get<SweepPreviewError>(result).code;
}

TEST(SweepPreviewValidationTest, RejectsInvalidEnvelopeMetadata) {
    auto invalidChannel = validPreview();
    invalidChannel.channelId = domain::ChannelId{0};
    EXPECT_EQ(
        rejectedCode(std::move(invalidChannel)),
        SweepPreviewErrorCode::InvalidIdentity);

    auto invalidSequence = validPreview();
    invalidSequence.sequenceNumber = 0;
    EXPECT_EQ(
        rejectedCode(std::move(invalidSequence)),
        SweepPreviewErrorCode::InvalidSequenceNumber);

    auto invalidTotal = validPreview();
    invalidTotal.totalPointCount = 1;
    EXPECT_EQ(
        rejectedCode(std::move(invalidTotal)),
        SweepPreviewErrorCode::InvalidTotalPointCount);
}

TEST(SweepPreviewValidationTest, RequiresUniqueValidTraceIdentities) {
    auto empty = validPreview();
    empty.traces.clear();
    EXPECT_EQ(
        rejectedCode(std::move(empty)),
        SweepPreviewErrorCode::EmptyTraceSet);

    auto invalid = validPreview();
    invalid.traces.front().measurementId = domain::MeasurementId{0};
    EXPECT_EQ(
        rejectedCode(std::move(invalid)),
        SweepPreviewErrorCode::InvalidTraceIdentity);

    auto duplicate = validPreview();
    duplicate.traces.push_back(duplicate.traces.front());
    EXPECT_EQ(
        rejectedCode(std::move(duplicate)),
        SweepPreviewErrorCode::DuplicateTraceId);
}

TEST(SweepPreviewValidationTest, EnforcesFormatUnitAndPayloadPairing) {
    auto payloadMismatch = validPreview();
    payloadMismatch.traces.front().format = display_model::TraceFormat::Smith;
    EXPECT_EQ(
        rejectedCode(std::move(payloadMismatch)),
        SweepPreviewErrorCode::SamplePayloadMismatch);

    auto wrongUnit = validPreview();
    std::get<CartesianTraceDisplaySamples>(wrongUnit.traces.front().samples)
        .unit = TraceDisplayUnit::Degree;
    EXPECT_EQ(
        rejectedCode(std::move(wrongUnit)),
        SweepPreviewErrorCode::UnsupportedValueUnit);

    auto unsupported = validPreview();
    unsupported.traces.front().format =
        static_cast<display_model::TraceFormat>(99);
    EXPECT_EQ(
        rejectedCode(std::move(unsupported)),
        SweepPreviewErrorCode::UnsupportedFormat);
}

TEST(SweepPreviewValidationTest, RejectsInvalidPrefixShapeAndValues) {
    auto mismatched = validPreview();
    std::get<CartesianTraceDisplaySamples>(mismatched.traces.front().samples)
        .values.pop_back();
    EXPECT_EQ(
        rejectedCode(std::move(mismatched)),
        SweepPreviewErrorCode::SampleCountMismatch);

    auto nonFinite = validPreview();
    nonFinite.traces.front().frequenciesHz.front() =
        std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        rejectedCode(std::move(nonFinite)),
        SweepPreviewErrorCode::NonFiniteValue);

    auto nonFiniteSample = validPreview();
    std::get<CartesianTraceDisplaySamples>(
        nonFiniteSample.traces.front().samples).values.front() =
        std::numeric_limits<double>::infinity();
    EXPECT_EQ(
        rejectedCode(std::move(nonFiniteSample)),
        SweepPreviewErrorCode::NonFiniteValue);

    auto unordered = validPreview();
    unordered.traces.front().frequenciesHz = {2.0e6, 1.0e6};
    EXPECT_EQ(
        rejectedCode(std::move(unordered)),
        SweepPreviewErrorCode::FrequencyNotStrictlyIncreasing);

    auto beyondTotal = validPreview();
    beyondTotal.totalPointCount = 2;
    beyondTotal.traces.front().frequenciesHz.push_back(3.0e6);
    std::get<CartesianTraceDisplaySamples>(beyondTotal.traces.front().samples)
        .values.push_back(-3.0);
    EXPECT_EQ(
        rejectedCode(std::move(beyondTotal)),
        SweepPreviewErrorCode::InvalidPrefixLength);
}

TEST(SweepPreviewValidationTest, RejectsRegressedOrRewrittenCumulativePrefix) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        exchange.publish(validPreview())));

    auto shorter = validPreview();
    shorter.traces.front().frequenciesHz.pop_back();
    std::get<CartesianTraceDisplaySamples>(shorter.traces.front().samples)
        .values.pop_back();
    auto result = exchange.publish(std::move(shorter));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(result));
    EXPECT_EQ(
        std::get<SweepPreviewError>(result).code,
        SweepPreviewErrorCode::ProgressRegression);

    auto rewritten = validPreview();
    rewritten.traces.front().frequenciesHz.push_back(3.0e6);
    auto& values = std::get<CartesianTraceDisplaySamples>(
        rewritten.traces.front().samples).values;
    values.front() = -99.0;
    values.push_back(-3.0);
    result = exchange.publish(std::move(rewritten));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(result));
    EXPECT_EQ(
        std::get<SweepPreviewError>(result).code,
        SweepPreviewErrorCode::ProgressRegression);

    auto changedMetadata = validPreview();
    changedMetadata.sequenceNumber = 10;
    result = exchange.publish(std::move(changedMetadata));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(result));
    EXPECT_EQ(
        std::get<SweepPreviewError>(result).code,
        SweepPreviewErrorCode::ProgressRegression);
}

TEST(SweepPreviewValidationTest, RejectionKeepsLastPreviewAndCursor) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        exchange.publish(validPreview())));
    auto invalidNext = validPreview();
    invalidNext.identity.sweepId = acquisition::SweepId{10};
    invalidNext.sequenceNumber = 10;
    invalidNext.traces.front().frequenciesHz.front() =
        std::numeric_limits<double>::quiet_NaN();

    const auto rejected = exchange.publish(std::move(invalidNext));
    const auto retained = exchange.waitForNext({});

    ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(rejected));
    ASSERT_TRUE(retained.has_value());
    const auto* available = std::get_if<SweepPreviewAvailable>(&*retained);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(available->cursor.value, 2U);
    EXPECT_EQ(available->preview->identity.sweepId, acquisition::SweepId{9});
}

}  // namespace
}  // namespace vna::application
