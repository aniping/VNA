#include <vna/domain/instrument.hpp>

#include <algorithm>

namespace vna::domain {
namespace {

bool isValidSweep(const SweepSettings& settings) {
    return settings.startFrequencyHz < settings.stopFrequencyHz &&
           settings.points >= 2;
}

}  // namespace

Result<ChannelId> Instrument::createChannel(SweepSettings settings) {
    if (!isValidSweep(settings)) {
        return Result<ChannelId>{
            DomainError{.code = DomainErrorCode::InvalidSweepSettings}};
    }

    const ChannelId id{nextChannelId_++};
    state_.channels.push_back(ChannelSnapshot{
        .id = id,
        .sweep = settings,
    });
    return Result<ChannelId>{id};
}

Result<ChannelId> Instrument::updateChannelSweep(
    ChannelId channelId,
    SweepSettings settings) {
    if (!isValidSweep(settings)) {
        return Result<ChannelId>{
            DomainError{.code = DomainErrorCode::InvalidSweepSettings}};
    }
    const auto channel = std::find_if(
        state_.channels.begin(),
        state_.channels.end(),
        [channelId](const ChannelSnapshot& candidate) {
            return candidate.id == channelId;
        });
    if (channel == state_.channels.end()) {
        return Result<ChannelId>{
            DomainError{.code = DomainErrorCode::ChannelNotFound}};
    }
    channel->sweep = settings;
    return Result<ChannelId>{channelId};
}

Result<MeasurementId> Instrument::createMeasurement(
    ChannelId channelId,
    MeasurementType type) {
    const auto channel = std::find_if(
        state_.channels.cbegin(),
        state_.channels.cend(),
        [channelId](const ChannelSnapshot& candidate) {
            return candidate.id == channelId;
        });
    if (channel == state_.channels.cend()) {
        return Result<MeasurementId>{
            DomainError{.code = DomainErrorCode::ChannelNotFound}};
    }

    const MeasurementId id{nextMeasurementId_++};
    state_.measurements.push_back(MeasurementSnapshot{
        .id = id,
        .channelId = channelId,
        .type = type,
    });
    return Result<MeasurementId>{id};
}

bool Instrument::containsMeasurement(MeasurementId measurementId) const {
    return std::find_if(
               state_.measurements.cbegin(),
               state_.measurements.cend(),
               [measurementId](const MeasurementSnapshot& candidate) {
                   return candidate.id == measurementId;
               }) != state_.measurements.cend();
}

InstrumentSnapshot Instrument::snapshot() const {
    return state_;
}

}  // namespace vna::domain
