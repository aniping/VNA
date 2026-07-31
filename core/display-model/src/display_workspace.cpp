#include <vna/display_model/display_workspace.hpp>

#include <algorithm>

namespace vna::display_model {

WindowId DisplayWorkspace::createWindow() {
    const WindowId id{nextWindowId_++};
    state_.windows.push_back(WindowSnapshot{.id = id});
    return id;
}

Result<TraceId> DisplayWorkspace::createTrace(
    WindowId windowId,
    domain::MeasurementId measurementId,
    TraceFormat format) {
    const auto window = std::find_if(
        state_.windows.cbegin(),
        state_.windows.cend(),
        [windowId](const WindowSnapshot& candidate) {
            return candidate.id == windowId;
        });
    if (window == state_.windows.cend()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::WindowNotFound}};
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

Result<TraceId> DisplayWorkspace::updateTraceFormat(
    TraceId traceId,
    TraceFormat format) {
    const auto trace = std::find_if(
        state_.traces.begin(),
        state_.traces.end(),
        [traceId](const TraceSnapshot& candidate) {
            return candidate.id == traceId;
        });
    if (trace == state_.traces.end()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::TraceNotFound}};
    }

    trace->format = format;
    return Result<TraceId>{traceId};
}

Result<TraceId> DisplayWorkspace::removeTrace(TraceId traceId) {
    const auto trace = std::find_if(
        state_.traces.cbegin(),
        state_.traces.cend(),
        [traceId](const TraceSnapshot& candidate) {
            return candidate.id == traceId;
        });
    if (trace == state_.traces.cend()) {
        return Result<TraceId>{
            DisplayError{.code = DisplayErrorCode::TraceNotFound}};
    }

    state_.traces.erase(trace);
    return Result<TraceId>{traceId};
}

DisplayWorkspaceSnapshot DisplayWorkspace::snapshot() const {
    return state_;
}

}  // namespace vna::display_model
