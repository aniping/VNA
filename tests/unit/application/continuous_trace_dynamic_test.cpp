#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <variant>
#include <vector>

#include <vna/application/command_bus.hpp>
#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {

StateSnapshot multiTraceState() {
    return {
        .stateRevision = 9,
        .control = {},
        .instrument = {
            .channels = {{
                .id = domain::ChannelId{1},
                .sweep = {1'000'000, 2'000'000, 3, 10'000, -10.5}}},
            .measurements = {
                {domain::MeasurementId{1}, domain::ChannelId{1},
                 domain::MeasurementType::S11},
                {domain::MeasurementId{2}, domain::ChannelId{1},
                 domain::MeasurementType::S21},
                {domain::MeasurementId{3}, domain::ChannelId{1},
                 domain::MeasurementType::S12},
                {domain::MeasurementId{4}, domain::ChannelId{1},
                 domain::MeasurementType::S22}}},
        .display = {.traces = {
            {display_model::TraceId{1}, display_model::WindowId{1},
             domain::MeasurementId{1},
             display_model::TraceFormat::LogMagnitude, std::nullopt},
            {display_model::TraceId{2}, display_model::WindowId{1},
             domain::MeasurementId{2}, display_model::TraceFormat::Phase,
             std::nullopt},
            {display_model::TraceId{3}, display_model::WindowId{1},
             domain::MeasurementId{3}, display_model::TraceFormat::Smith,
             std::nullopt},
            {display_model::TraceId{4}, display_model::WindowId{1},
             domain::MeasurementId{4},
             display_model::TraceFormat::LogMagnitude, std::nullopt},
            {display_model::TraceId{5}, display_model::WindowId{1},
             domain::MeasurementId{2}, display_model::TraceFormat::Smith,
             std::nullopt}}},
    };
}

void expectCommonIdentity(const TraceDisplayFrameSet& set) {
    for (std::size_t index = 0; index < set.frames.size(); ++index) {
        const auto& frame = set.frames[index];
        EXPECT_EQ(frame.frameId, frames::FrameId{1});
        EXPECT_EQ(frame.traceId, display_model::TraceId{index + 1});
        EXPECT_EQ(frame.stateRevision, 9U);
        EXPECT_EQ(frame.generation, 1U);
        EXPECT_EQ(frame.sequenceNumber, 1U);
        EXPECT_EQ(frame.frequenciesHz,
                  (std::vector<double>{1'000'000, 1'500'000, 2'000'000}));
    }
}

void expectKnownSamples(const TraceDisplayFrameSet& set) {
    const auto& s11 =
        std::get<CartesianTraceDisplaySamples>(set.frames[0].samples);
    EXPECT_EQ(s11.unit, TraceDisplayUnit::Decibel);
    EXPECT_NEAR(s11.values[0], 20.0 * std::log10(1.1), 1e-12);
    const auto& s21Phase =
        std::get<CartesianTraceDisplaySamples>(set.frames[1].samples);
    EXPECT_EQ(s21Phase.unit, TraceDisplayUnit::Degree);
    EXPECT_DOUBLE_EQ(s21Phase.values[0], 0.0);
    const auto& s12 =
        std::get<ComplexTraceDisplaySamples>(set.frames[2].samples);
    EXPECT_EQ(s12.unit, TraceDisplayUnit::Unitless);
    EXPECT_DOUBLE_EQ(s12.values[0].real, 1.1);
    EXPECT_DOUBLE_EQ(s12.values[0].imaginary, 0.0);
    const auto& s22 =
        std::get<CartesianTraceDisplaySamples>(set.frames[3].samples);
    EXPECT_NEAR(s22.values[0], 20.0 * std::log10(1.2), 1e-12);
    const auto& duplicateS21 =
        std::get<ComplexTraceDisplaySamples>(set.frames[4].samples);
    EXPECT_DOUBLE_EQ(duplicateS21.values[0].real, 1.2);
}

TEST(ContinuousTraceDynamicTest, PublishesAllConfiguredTracesFromOneRawSweep) {
    std::atomic<unsigned int> sourceCalls{0};
    const auto source = [&sourceCalls](
                            const acquisition::ContinuousAcquisitionPlan&,
                            std::uint64_t sequence,
                            std::stop_token) {
        ++sourceCalls;
        if (sequence == 1) {
            return frames::Result<frames::RawReceiverPayload>{
                acquisition::test_support::validPayload(sequence)};
        }
        return frames::Result<frames::RawReceiverPayload>{
            frames::FrameError{frames::FrameErrorCode::InvalidPortCount}};
    };
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{5};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository, multiTraceState()};
    ContinuousTracePublisher publisher{acquisition, catalog};
    acquisition.join();
    publisher.join();

    const auto set = repository.latestFrameSet();
    ASSERT_NE(set, nullptr);
    ASSERT_EQ(set->frames.size(), 5U);
    EXPECT_EQ(sourceCalls.load(), 2U);
    expectCommonIdentity(*set);
    expectKnownSamples(*set);
}

}  // namespace
}  // namespace vna::application
