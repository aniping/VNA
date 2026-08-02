#include <vna/display_model/display_workspace.hpp>

#include <algorithm>
#include <cmath>

namespace vna::display_model {
namespace {
constexpr double kVerticalDivisions = 10.0;
}

std::optional<DisplayWorkspace::CartesianScaleState>
DisplayWorkspace::defaultScaleFor(TraceFormat format) {
    if (format != TraceFormat::LogMagnitude) {
        return std::nullopt;
    }
    return CartesianScaleState{
        .scalePerDivision = 10.0,
        .referenceValue = 0.0,
        .referencePosition = 8.0,
    };
}

CartesianScaleSnapshot DisplayWorkspace::scaleSnapshot(
    const CartesianScaleState& scale) {
    return CartesianScaleSnapshot{
        .scalePerDivision = scale.scalePerDivision,
        .referenceValue = scale.referenceValue,
        .referencePosition = scale.referencePosition,
        .minimum = scale.referenceValue -
            scale.referencePosition * scale.scalePerDivision,
        .maximum = scale.referenceValue +
            (kVerticalDivisions - scale.referencePosition) *
                scale.scalePerDivision,
        .unit = ScaleUnit::Decibel,
    };
}

bool DisplayWorkspace::validScale(const CartesianScaleState& scale) {
    if (!std::isfinite(scale.scalePerDivision) ||
        scale.scalePerDivision <= 0.0 ||
        !std::isfinite(scale.referenceValue) ||
        !std::isfinite(scale.referencePosition) ||
        scale.referencePosition < 0.0 ||
        scale.referencePosition > kVerticalDivisions) {
        return false;
    }
    const auto snapshot = scaleSnapshot(scale);
    return std::isfinite(snapshot.minimum) && std::isfinite(snapshot.maximum);
}

WindowId DisplayWorkspace::createWindow() {
    const WindowId id{nextWindowId_++};
    windows_.push_back(WindowSnapshot{.id = id});
    return id;
}

Result<TraceId> DisplayWorkspace::createTrace(
    WindowId windowId,
    domain::MeasurementId measurementId,
    TraceFormat format) {
    const auto window = std::find_if(
        windows_.cbegin(),
        windows_.cend(),
        [windowId](const WindowSnapshot& candidate) {
            return candidate.id == windowId;
        });
    if (window == windows_.cend()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::WindowNotFound}};
    }

    const TraceId id{nextTraceId_++};
    traces_.push_back(TraceState{
        .id = id,
        .windowId = windowId,
        .measurementId = measurementId,
        .format = format,
        .scale = defaultScaleFor(format),
    });
    return Result<TraceId>{id};
}

Result<TraceId> DisplayWorkspace::createTrace(
    WindowId windowId,
    domain::MeasurementId measurementId,
    TraceFormat format,
    CartesianScaleSettings initialScale) {
    const auto window = std::find_if(
        windows_.cbegin(),
        windows_.cend(),
        [windowId](const WindowSnapshot& candidate) {
            return candidate.id == windowId;
        });
    if (window == windows_.cend()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::WindowNotFound}};
    }
    if (format != TraceFormat::LogMagnitude) {
        return Result<TraceId>{DisplayError{
            .code = DisplayErrorCode::ScaleNotSupportedForFormat}};
    }
    const CartesianScaleState scale{
        initialScale.scalePerDivision,
        initialScale.referenceValue,
        initialScale.referencePosition};
    if (!validScale(scale)) {
        // This established cause covers Cartesian scale validation as a whole;
        // retaining it avoids inventing a second classification for the same
        // atomic settings boundary.
        return Result<TraceId>{DisplayError{
            .code = DisplayErrorCode::InvalidScalePerDivision}};
    }

    const TraceId id{nextTraceId_++};
    traces_.push_back(TraceState{
        id, windowId, measurementId, format, scale});
    return Result<TraceId>{id};
}

Result<TraceId> DisplayWorkspace::updateTraceFormat(
    TraceId traceId,
    TraceFormat format) {
    const auto trace = std::find_if(
        traces_.begin(),
        traces_.end(),
        [traceId](const TraceState& candidate) {
            return candidate.id == traceId;
        });
    if (trace == traces_.end()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::TraceNotFound}};
    }
    if (trace->format == format) {
        return Result<TraceId>{traceId};
    }

    trace->format = format;
    trace->scale = defaultScaleFor(format);
    return Result<TraceId>{traceId};
}

Result<TraceId> DisplayWorkspace::updateTraceMeasurement(
    TraceId traceId,
    domain::MeasurementId measurementId) {
    const auto trace = std::find_if(
        traces_.begin(),
        traces_.end(),
        [traceId](const TraceState& candidate) {
            return candidate.id == traceId;
        });
    if (trace == traces_.end()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::TraceNotFound}};
    }

    trace->measurementId = measurementId;
    return Result<TraceId>{traceId};
}

Result<TraceId> DisplayWorkspace::updateTraceScalePerDivision(
    TraceId traceId,
    double scalePerDivision) {
    const auto trace = std::find_if(
        traces_.begin(),
        traces_.end(),
        [traceId](const TraceState& candidate) {
            return candidate.id == traceId;
        });
    if (trace == traces_.end()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::TraceNotFound}};
    }
    if (!trace->scale.has_value()) {
        return Result<TraceId>{DisplayError{
            .code = DisplayErrorCode::ScaleNotSupportedForFormat}};
    }
    if (!std::isfinite(scalePerDivision) || scalePerDivision <= 0.0) {
        return Result<TraceId>{DisplayError{
            .code = DisplayErrorCode::InvalidScalePerDivision}};
    }

    auto candidate = trace->scale.value();
    candidate.scalePerDivision = scalePerDivision;
    if (!validScale(candidate)) {
        return Result<TraceId>{DisplayError{
            .code = DisplayErrorCode::InvalidScalePerDivision}};
    }

    trace->scale = candidate;
    return Result<TraceId>{traceId};
}

Result<TraceId> DisplayWorkspace::removeTrace(TraceId traceId) {
    const auto trace = std::find_if(
        traces_.cbegin(),
        traces_.cend(),
        [traceId](const TraceState& candidate) {
            return candidate.id == traceId;
        });
    if (trace == traces_.cend()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::TraceNotFound}};
    }

    traces_.erase(trace);
    return Result<TraceId>{traceId};
}

DisplayWorkspaceSnapshot DisplayWorkspace::snapshot() const {
    DisplayWorkspaceSnapshot snapshot{
        .windows = windows_,
        .traces = {},
    };
    snapshot.traces.reserve(traces_.size());
    for (const auto& trace : traces_) {
        std::optional<CartesianScaleSnapshot> scale;
        if (trace.scale.has_value()) {
            scale = scaleSnapshot(trace.scale.value());
        }
        snapshot.traces.push_back(TraceSnapshot{
            .id = trace.id,
            .windowId = trace.windowId,
            .measurementId = trace.measurementId,
            .format = trace.format,
            .scale = scale,
        });
    }
    return snapshot;
}

}  // namespace vna::display_model
