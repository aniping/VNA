#include <vna/application/command_bus.hpp>
#include <vna/application/single_sweep_command_handler.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace vna::application {
namespace {

// The first vertical slice accepts one unambiguous S11/LogMagnitude path.
// This runs under the CommandBus lock: only value capture and bounded admission
// happen here; simulation and frame processing remain on the executor worker.
std::optional<CapturedSingleSweep> captureSupportedSweep(
    const domain::ChannelSnapshot& channel,
    const domain::InstrumentSnapshot& instrument,
    const display_model::DisplayWorkspaceSnapshot& display,
    const CommandEnvelope& envelope,
    std::uint64_t revision) {
    const auto forChannel = [&channel](const auto& measurement) {
        return measurement.channelId == channel.id;
    };
    const auto measurement = std::find_if(
        instrument.measurements.cbegin(), instrument.measurements.cend(),
        forChannel);
    if (measurement == instrument.measurements.cend() ||
        std::count_if(instrument.measurements.cbegin(),
                      instrument.measurements.cend(), forChannel) != 1 ||
        measurement->type != domain::MeasurementType::S11) {
        return std::nullopt;
    }
    const auto forMeasurement = [&measurement](const auto& trace) {
        return trace.measurementId == measurement->id;
    };
    const auto trace = std::find_if(
        display.traces.cbegin(), display.traces.cend(), forMeasurement);
    if (trace == display.traces.cend() ||
        std::count_if(display.traces.cbegin(), display.traces.cend(),
                      forMeasurement) != 1 ||
        trace->format != display_model::TraceFormat::LogMagnitude) {
        return std::nullopt;
    }
    return CapturedSingleSweep{
        envelope.commandId, envelope.sessionId, revision,
        channel, *measurement, *trace};
}

}  // namespace

CommandResult CommandBus::execute(
    const StartSingleSweepCommand& command,
    const CommandEnvelope& envelope) {
    const auto instrument = instrument_.snapshot();
    const auto channel = std::find_if(
        instrument.channels.cbegin(),
        instrument.channels.cend(),
        [&command](const domain::ChannelSnapshot& candidate) {
            return candidate.id == command.channelId;
        });
    if (channel == instrument.channels.cend()) {
        return domainError(domain::DomainError{
            .code = domain::DomainErrorCode::ChannelNotFound});
    }
    const auto capture = captureSupportedSweep(
        *channel, instrument, displayWorkspace_.snapshot(),
        envelope, stateRevision_);
    if (!capture.has_value()) {
        return applicationError(
            ApplicationErrorCode::UnsupportedSweepConfiguration);
    }
    const auto submitted =
        singleSweepHandler_.submit(std::move(*capture));
    if (std::holds_alternative<SingleSweepSubmitError>(submitted)) {
        return applicationError(ApplicationErrorCode::ResourceBusy);
    }
    const auto operationId = std::get<OperationId>(submitted);
    return CommandResult{
        .stateRevision = stateRevision_,
        .outcome = CommandSuccess{.value = CommandValue{operationId}},
    };
}

}  // namespace vna::application
