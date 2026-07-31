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

TEST(InstrumentTest, ReportsMeasurementExistence) {
    Instrument instrument;
    EXPECT_FALSE(instrument.containsMeasurement(MeasurementId{1}));

    const auto channel = instrument.createChannel(validSweep());
    ASSERT_TRUE(channel.hasValue());
    const auto measurement =
        instrument.createMeasurement(channel.value(), MeasurementType::S11);
    ASSERT_TRUE(measurement.hasValue());

    EXPECT_TRUE(instrument.containsMeasurement(measurement.value()));
}

}  // namespace
}  // namespace vna::domain
