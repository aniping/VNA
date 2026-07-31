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

WindowId Instrument::createWindow() {
    const WindowId id{nextWindowId_++};
    state_.windows.push_back(WindowSnapshot{.id = id});
    return id;
}

Result<TraceId> Instrument::createTrace(
    WindowId windowId,
    MeasurementId measurementId,
    TraceFormat format) {
    const auto measurement = std::find_if(
        state_.measurements.cbegin(),
        state_.measurements.cend(),
        [measurementId](const MeasurementSnapshot& candidate) {
            return candidate.id == measurementId;
        });
    if (measurement == state_.measurements.cend()) {
        return Result<TraceId>{
            DomainError{.code = DomainErrorCode::MeasurementNotFound}};
    }

    const auto window = std::find_if(
        state_.windows.cbegin(),
        state_.windows.cend(),
        [windowId](const WindowSnapshot& candidate) {
            return candidate.id == windowId;
        });
    if (window == state_.windows.cend()) {
        return Result<TraceId>{
            DomainError{.code = DomainErrorCode::WindowNotFound}};
    }

    const TraceId id{nextTraceId_++};
    state_.traces.push_back(TraceSnapshot{
        .id = id,
        .windowId = windowId,
        .measurementId = measurementId,
        .format = format,
    });
    return Result<TraceId>{id};
}

bool Instrument::removeTrace(TraceId traceId) {
    const auto trace = std::find_if(
        state_.traces.cbegin(),
        state_.traces.cend(),
        [traceId](const TraceSnapshot& candidate) {
            return candidate.id == traceId;
        });
    if (trace == state_.traces.cend()) {
        return false;
    }

    state_.traces.erase(trace);
    return true;
}

InstrumentSnapshot Instrument::snapshot() const {
    return state_;
}

}  // namespace vna::domain
