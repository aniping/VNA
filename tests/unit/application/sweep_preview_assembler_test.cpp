#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <variant>
#include <vector>

#include <vna/application/sweep_preview_assembler.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {

TracePublicationTarget target(
    std::uint64_t traceId,
    std::uint64_t measurementId,
    domain::MeasurementType type,
    display_model::TraceFormat format) {
    return {
        .measurement = {
            .id = domain::MeasurementId{measurementId},
            .channelId = domain::ChannelId{2},
            .type = type,
        },
        .trace = {
            .id = display_model::TraceId{traceId},
            .windowId = display_model::WindowId{traceId},
            .measurementId = domain::MeasurementId{measurementId},
            .format = format,
        },
    };
}

SweepPreviewAssemblyPlan assemblyPlan() {
    auto publication = std::make_shared<const TracePublicationPlan>(
        TracePublicationPlan{
            .generation = 1,
            .stateRevision = 7,
            .channelId = domain::ChannelId{2},
            .targets = {
                target(3, 2, domain::MeasurementType::S12,
                       display_model::TraceFormat::Smith),
                target(2, 1, domain::MeasurementType::S11,
                       display_model::TraceFormat::Phase),
                target(1, 1, domain::MeasurementType::S11,
                       display_model::TraceFormat::LogMagnitude),
            },
        });
    return {
        .acquisition = acquisition::test_support::validPlan(),
        .publication = std::move(publication),
        .sweepId = acquisition::SweepId{5},
        .sequenceNumber = 9,
    };
}

acquisition::RawSweepPointRange range(
    const frames::RawSourceState& source,
    std::uint32_t first,
    std::uint32_t count) {
    const auto begin = source.samples.cbegin() + first;
    return {
        .sourcePort = source.sourcePort,
        .firstPoint = first,
        .samples = {begin, begin + count},
    };
}

const SweepPreview& preview(const SweepPreviewAssemblyResult& result) {
    return std::get<SweepPreview>(result);
}

SweepPreviewAssemblyErrorCode errorCode(
    const SweepPreviewAssemblyResult& result) {
    return std::get<SweepPreviewAssemblyError>(result).code;
}

TEST(SweepPreviewAssemblerTest, BuildsCumulativePrefixesAcrossSourceStates) {
    SweepPreviewAssembler assembler{assemblyPlan()};
    const auto payload = acquisition::test_support::validPayload(1);

    const auto first = assembler.append(range(payload.sourceStates[0], 0, 2));
    ASSERT_TRUE(std::holds_alternative<SweepPreview>(first));
    ASSERT_EQ(preview(first).traces.size(), 2U);
    EXPECT_EQ(preview(first).identity,
              (SweepPreviewIdentity{1, acquisition::SweepId{5}}));
    EXPECT_EQ(preview(first).stateRevision, 7U);
    EXPECT_EQ(preview(first).sequenceNumber, 9U);
    EXPECT_EQ(preview(first).traces[0].frequenciesHz,
              (std::vector<double>{1'000'000.0, 1'500'000.0}));

    const auto second = assembler.append(range(payload.sourceStates[0], 2, 1));
    ASSERT_TRUE(std::holds_alternative<SweepPreview>(second));
    ASSERT_EQ(preview(second).traces.size(), 2U);
    EXPECT_EQ(preview(second).traces[0].frequenciesHz.size(), 3U);
    const auto* magnitude = std::get_if<CartesianTraceDisplaySamples>(
        &preview(second).traces[0].samples);
    ASSERT_NE(magnitude, nullptr);
    EXPECT_NEAR(magnitude->values[0], 20.0 * std::log10(1.1), 1e-12);

    const auto third = assembler.append(range(payload.sourceStates[1], 0, 3));
    ASSERT_TRUE(std::holds_alternative<SweepPreview>(third));
    ASSERT_EQ(preview(third).traces.size(), 3U);
    const auto* smith = std::get_if<ComplexTraceDisplaySamples>(
        &preview(third).traces[2].samples);
    ASSERT_NE(smith, nullptr);
    EXPECT_EQ(smith->values.size(), 3U);
    EXPECT_NEAR(smith->values[0].real, 1.1, 1e-12);
    EXPECT_DOUBLE_EQ(smith->values[0].imaginary, 0.0);
}

TEST(SweepPreviewAssemblerTest, RejectedRangeDoesNotAdvanceRawProgress) {
    SweepPreviewAssembler assembler{assemblyPlan()};
    auto payload = acquisition::test_support::validPayload(1);

    const auto gap = assembler.append(range(payload.sourceStates[0], 1, 1));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewAssemblyError>(gap));
    EXPECT_EQ(errorCode(gap), SweepPreviewAssemblyErrorCode::RangeGap);

    payload.sourceStates[0].samples[0].reference = {0.0, 0.0};
    const auto invalid =
        assembler.append(range(payload.sourceStates[0], 0, 1));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewAssemblyError>(invalid));
    EXPECT_EQ(
        errorCode(invalid),
        SweepPreviewAssemblyErrorCode::MeasurementSynthesisFailed);
    ASSERT_TRUE(std::get<SweepPreviewAssemblyError>(invalid).cause.has_value());
    EXPECT_EQ(std::get<SweepPreviewAssemblyError>(invalid).cause->code,
              frames::FrameErrorCode::ZeroReference);

    payload = acquisition::test_support::validPayload(1);
    const auto recovered =
        assembler.append(range(payload.sourceStates[0], 0, 1));
    ASSERT_TRUE(std::holds_alternative<SweepPreview>(recovered));
    EXPECT_EQ(preview(recovered).traces[0].frequenciesHz.size(), 1U);

    const auto overlap =
        assembler.append(range(payload.sourceStates[0], 0, 1));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewAssemblyError>(overlap));
    EXPECT_EQ(errorCode(overlap), SweepPreviewAssemblyErrorCode::RangeOverlap);
}

}  // namespace
}  // namespace vna::application
