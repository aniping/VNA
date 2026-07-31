#include <gtest/gtest.h>

#include <vna/domain/instrument.hpp>

namespace vna::domain {
namespace {

constexpr SweepSettings validSweep() {
    return SweepSettings{
        .startFrequencyHz = 1'000'000'000,
        .stopFrequencyHz = 2'000'000'000,
        .points = 201,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
    };
}

TEST(InstrumentTest, MultipleTracesReuseOneMeasurement) {
    Instrument instrument;

    const auto channel = instrument.createChannel(validSweep());
    ASSERT_TRUE(channel.hasValue());

    const auto measurement =
        instrument.createMeasurement(channel.value(), MeasurementType::S11);
    ASSERT_TRUE(measurement.hasValue());

    const auto window = instrument.createWindow();
    const auto logMagnitude = instrument.createTrace(
        window, measurement.value(), TraceFormat::LogMagnitude);
    const auto phase = instrument.createTrace(
        window, measurement.value(), TraceFormat::Phase);

    ASSERT_TRUE(logMagnitude.hasValue());
    ASSERT_TRUE(phase.hasValue());

    const auto snapshot = instrument.snapshot();
    EXPECT_EQ(snapshot.measurements.size(), 1U);
    EXPECT_EQ(snapshot.traces.size(), 2U);
}

TEST(InstrumentTest, InvalidSweepDoesNotChangeState) {
    Instrument instrument;
    auto sweep = validSweep();
    sweep.startFrequencyHz = 2'000'000'000;
    sweep.stopFrequencyHz = 1'000'000'000;

    const auto channel = instrument.createChannel(sweep);

    ASSERT_FALSE(channel.hasValue());
    EXPECT_EQ(channel.error().code, DomainErrorCode::InvalidSweepSettings);
    EXPECT_TRUE(instrument.snapshot().channels.empty());
}

TEST(InstrumentTest, SweepRequiresAtLeastTwoPoints) {
    Instrument instrument;
    auto sweep = validSweep();
    sweep.points = 1;

    const auto channel = instrument.createChannel(sweep);

    ASSERT_FALSE(channel.hasValue());
    EXPECT_EQ(channel.error().code, DomainErrorCode::InvalidSweepSettings);
    EXPECT_TRUE(instrument.snapshot().channels.empty());
}

TEST(InstrumentTest, MeasurementCannotReferenceMissingChannel) {
    Instrument instrument;

    const auto measurement =
        instrument.createMeasurement(ChannelId{42}, MeasurementType::S11);

    ASSERT_FALSE(measurement.hasValue());
    EXPECT_EQ(measurement.error().code, DomainErrorCode::ChannelNotFound);
    EXPECT_TRUE(instrument.snapshot().measurements.empty());
}

TEST(InstrumentTest, TraceCannotReferenceMissingMeasurement) {
    Instrument instrument;
    const auto window = instrument.createWindow();

    const auto trace = instrument.createTrace(
        window, MeasurementId{42}, TraceFormat::LogMagnitude);

    ASSERT_FALSE(trace.hasValue());
    EXPECT_EQ(trace.error().code, DomainErrorCode::MeasurementNotFound);
    EXPECT_TRUE(instrument.snapshot().traces.empty());
}

TEST(InstrumentTest, TraceCannotReferenceMissingWindow) {
    Instrument instrument;
    const auto channel = instrument.createChannel(validSweep());
    ASSERT_TRUE(channel.hasValue());
    const auto measurement =
        instrument.createMeasurement(channel.value(), MeasurementType::S11);
    ASSERT_TRUE(measurement.hasValue());

    const auto trace = instrument.createTrace(
        WindowId{42}, measurement.value(), TraceFormat::LogMagnitude);

    ASSERT_FALSE(trace.hasValue());
    EXPECT_EQ(trace.error().code, DomainErrorCode::WindowNotFound);
    EXPECT_TRUE(instrument.snapshot().traces.empty());
}

TEST(InstrumentTest, RemovingTraceKeepsItsMeasurement) {
    Instrument instrument;
    const auto channel = instrument.createChannel(validSweep());
    ASSERT_TRUE(channel.hasValue());
    const auto measurement =
        instrument.createMeasurement(channel.value(), MeasurementType::S11);
    ASSERT_TRUE(measurement.hasValue());
    const auto window = instrument.createWindow();
    const auto trace = instrument.createTrace(
        window, measurement.value(), TraceFormat::LogMagnitude);
    ASSERT_TRUE(trace.hasValue());

    EXPECT_TRUE(instrument.removeTrace(trace.value()));

    const auto snapshot = instrument.snapshot();
    EXPECT_TRUE(snapshot.traces.empty());
    EXPECT_EQ(snapshot.measurements.size(), 1U);
}

}  // namespace
}  // namespace vna::domain
