#include <vna/application/command_bus.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace vna::application {
namespace {

constexpr std::array kSParameters{
    domain::MeasurementType::S11,
    domain::MeasurementType::S12,
    domain::MeasurementType::S21,
    domain::MeasurementType::S22,
};

const display_model::TraceSnapshot* findTrace(
    const display_model::DisplayWorkspaceSnapshot& display,
    display_model::TraceId traceId) {
    const auto found = std::find_if(
        display.traces.cbegin(), display.traces.cend(),
        [traceId](const auto& trace) { return trace.id == traceId; });
    return found == display.traces.cend() ? nullptr : &*found;
}

const domain::MeasurementSnapshot* findMeasurement(
    const domain::InstrumentSnapshot& instrument,
    domain::MeasurementId measurementId) {
    const auto found = std::find_if(
        instrument.measurements.cbegin(), instrument.measurements.cend(),
        [measurementId](const auto& item) {
            return item.id == measurementId;
        });
    return found == instrument.measurements.cend() ? nullptr : &*found;
}

std::vector<const domain::MeasurementSnapshot*> channelMeasurements(
    const domain::InstrumentSnapshot& instrument,
    domain::ChannelId channelId) {
    std::vector<const domain::MeasurementSnapshot*> result;
    for (const auto& measurement : instrument.measurements) {
        if (measurement.channelId == channelId) {
            result.push_back(&measurement);
        }
    }
    return result;
}

bool hasUniqueSupportedTypes(
    const std::vector<const domain::MeasurementSnapshot*>& measurements) {
    for (const auto type : kSParameters) {
        if (std::count_if(
                measurements.cbegin(), measurements.cend(),
                [type](const auto* item) { return item->type == type; }) > 1) {
            return false;
        }
    }
    return std::all_of(
        measurements.cbegin(), measurements.cend(), [](const auto* item) {
            return std::find(
                       kSParameters.cbegin(), kSParameters.cend(), item->type) !=
                kSParameters.cend();
        });
}

std::optional<domain::MeasurementId> measurementFor(
    const std::vector<const domain::MeasurementSnapshot*>& measurements,
    domain::MeasurementType type) {
    const auto found = std::find_if(
        measurements.cbegin(), measurements.cend(),
        [type](const auto* item) { return item->type == type; });
    return found == measurements.cend()
        ? std::nullopt
        : std::optional<domain::MeasurementId>{(*found)->id};
}

bool isComplete(
    const domain::InstrumentSnapshot& instrument,
    const display_model::DisplayWorkspaceSnapshot& display,
    domain::ChannelId channelId) {
    if (display.traces.size() != kSParameters.size() ||
        display.windows.size() != kSParameters.size()) {
        return false;
    }
    for (std::size_t index = 0; index < kSParameters.size(); ++index) {
        const auto expectedId = static_cast<std::uint64_t>(index + 1);
        const auto& window = display.windows[index];
        const auto& trace = display.traces[index];
        const auto* measurement =
            findMeasurement(instrument, trace.measurementId);
        if (window.id != display_model::WindowId{expectedId} ||
            trace.id != display_model::TraceId{expectedId} ||
            trace.windowId != window.id || measurement == nullptr ||
            measurement->channelId != channelId ||
            measurement->type != kSParameters[index]) {
            return false;
        }
    }
    return true;
}

bool isPresetShape(
    const domain::InstrumentSnapshot& instrument,
    const display_model::DisplayWorkspaceSnapshot& display,
    const display_model::TraceSnapshot& anchor,
    const domain::MeasurementSnapshot& measurement) {
    return instrument.channels.size() == 1 && display.windows.size() == 1 &&
        display.traces.size() == 1 && display.traces.front().id == anchor.id &&
        display.windows.front().id == anchor.windowId &&
        anchor.format == display_model::TraceFormat::LogMagnitude &&
        measurement.type == domain::MeasurementType::S21;
}

std::optional<domain::MeasurementId> ensureMeasurement(
    domain::Instrument& instrument,
    domain::ChannelId channelId,
    domain::MeasurementType type) {
    const auto snapshot = instrument.snapshot();
    const auto existing = measurementFor(
        channelMeasurements(snapshot, channelId), type);
    if (existing.has_value()) {
        return existing;
    }
    const auto created = instrument.createMeasurement(channelId, type);
    return created.hasValue()
        ? std::optional<domain::MeasurementId>{created.value()}
        : std::nullopt;
}

bool appendCanonicalDiagrams(
    domain::Instrument& instrument,
    display_model::DisplayWorkspace& display,
    domain::ChannelId channelId,
    display_model::TraceId anchorTraceId) {
    std::vector<domain::MeasurementId> measurements;
    measurements.reserve(kSParameters.size());
    for (const auto type : kSParameters) {
        const auto measurement = ensureMeasurement(instrument, channelId, type);
        if (!measurement.has_value()) {
            return false;
        }
        measurements.push_back(*measurement);
    }
    if (!display.updateTraceMeasurement(
            anchorTraceId, measurements.front()).hasValue()) {
        return false;
    }
    for (std::size_t index = 1; index < measurements.size(); ++index) {
        const auto window = display.createWindow();
        const auto trace = display.createTrace(
            window,
            measurements[index],
            display_model::TraceFormat::LogMagnitude);
        if (!trace.hasValue()) {
            return false;
        }
    }
    return true;
}

}  // namespace

CommandResult CommandBus::execute(const EnsureAllSParametersCommand& command) {
    const auto instrument = instrument_.snapshot();
    const auto display = displayWorkspace_.snapshot();
    const auto* anchor = findTrace(display, command.traceId);
    if (anchor == nullptr) {
        return displayError(display_model::DisplayError{
            .code = display_model::DisplayErrorCode::TraceNotFound});
    }
    const auto* measurement = findMeasurement(instrument, anchor->measurementId);
    if (measurement == nullptr) {
        return applicationError(
            ApplicationErrorCode::TraceConfigurationRejected);
    }
    // This slice owns one acquisition Channel; silently accepting another one
    // would invent cross-Channel layout semantics that ZNB does not specify here.
    if (instrument.channels.size() != 1) {
        return applicationError(
            ApplicationErrorCode::TraceConfigurationRejected);
    }
    const auto measurements =
        channelMeasurements(instrument, measurement->channelId);
    if (!hasUniqueSupportedTypes(measurements)) {
        return applicationError(
            ApplicationErrorCode::TraceConfigurationRejected);
    }
    if (isComplete(instrument, display, measurement->channelId)) {
        return succeededWithoutRevision(CommandValue{anchor->id});
    }
    if (!isPresetShape(instrument, display, *anchor, *measurement)) {
        return applicationError(
            ApplicationErrorCode::TraceConfigurationRejected);
    }

    auto candidateInstrument = instrument_;
    auto candidateDisplay = displayWorkspace_;
    if (!appendCanonicalDiagrams(
            candidateInstrument,
            candidateDisplay,
            measurement->channelId,
            anchor->id)) {
        return applicationError(
            ApplicationErrorCode::TraceConfigurationRejected);
    }
    return commitTraceConfiguration(
        std::move(candidateInstrument),
        std::move(candidateDisplay),
        CommandValue{anchor->id});
}

}  // namespace vna::application
