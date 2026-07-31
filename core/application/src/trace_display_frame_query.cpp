#include <vna/application/trace_display_frame_query.hpp>

#include <algorithm>

namespace vna::application {

TraceDisplayFrameQuery::TraceDisplayFrameQuery(
    const CommandBus& commandBus,
    const TraceDisplayFrameRepository& repository)
    : commandBus_(commandBus), repository_(repository) {}

TraceDisplayFrameQueryOutcome TraceDisplayFrameQuery::latest(
    display_model::TraceId traceId) const {
    const auto snapshot = commandBus_.snapshot();
    const auto trace = std::find_if(
        snapshot.display.traces.cbegin(),
        snapshot.display.traces.cend(),
        [traceId](const display_model::TraceSnapshot& candidate) {
            return candidate.id == traceId;
        });
    if (trace == snapshot.display.traces.cend()) {
        return TraceDisplayFrameQueryError{
            .code = TraceDisplayFrameQueryErrorCode::TraceNotFound};
    }
    const auto frame = repository_.latest(traceId);
    if (frame == nullptr || frame->traceId != traceId ||
        trace->format != display_model::TraceFormat::LogMagnitude ||
        frame->format != trace->format) {
        return TraceDisplayFrameQueryError{
            .code = TraceDisplayFrameQueryErrorCode::FrameNotAvailable};
    }
    return frame;
}

}  // namespace vna::application
