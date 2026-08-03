#include <vna/application/command_bus.hpp>

#include <algorithm>
#include <utility>

namespace vna::application {
namespace {

bool sameSweep(
    const domain::SweepSettings& left,
    const domain::SweepSettings& right) noexcept {
    return left.startFrequencyHz == right.startFrequencyHz &&
        left.stopFrequencyHz == right.stopFrequencyHz &&
        left.points == right.points &&
        left.ifBandwidthHz == right.ifBandwidthHz &&
        left.powerDbm == right.powerDbm;
}

}  // namespace

CommandResult CommandBus::execute(const CreateChannelCommand& command) {
    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto channel = candidateInstrument.createChannel(command.sweep);
    if (!channel.hasValue()) {
        return domainError(channel.error());
    }
    return commitConfiguration(
        std::move(candidateInstrument), std::move(candidateDisplay),
        CommandValue{channel.value()});
}

CommandResult CommandBus::execute(const UpdateChannelSweepCommand& command) {
    const auto current = instrument_.snapshot();
    const auto found = std::find_if(
        current.channels.cbegin(), current.channels.cend(),
        [&command](const auto& channel) {
            return channel.id == command.channelId;
        });
    if (found != current.channels.cend() && sameSweep(found->sweep, command.sweep)) {
        return succeededWithoutRevision(CommandValue{command.channelId});
    }
    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto channel =
        candidateInstrument.updateChannelSweep(command.channelId, command.sweep);
    if (!channel.hasValue()) {
        return domainError(channel.error());
    }
    return commitConfiguration(
        std::move(candidateInstrument), std::move(candidateDisplay),
        CommandValue{channel.value()});
}

CommandResult CommandBus::execute(const CreateMeasurementCommand& command) {
    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto measurement =
        candidateInstrument.createMeasurement(command.channelId, command.type);
    if (!measurement.hasValue()) {
        return domainError(measurement.error());
    }
    return commitConfiguration(
        std::move(candidateInstrument), std::move(candidateDisplay),
        CommandValue{measurement.value()});
}

CommandResult CommandBus::execute(const CreateWindowCommand&) {
    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    const auto window = candidateDisplay.createWindow();
    return commitConfiguration(
        std::move(candidateInstrument), std::move(candidateDisplay),
        CommandValue{window});
}

}  // namespace vna::application
