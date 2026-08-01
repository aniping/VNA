#include <vna/application/trace_display_frame_query.hpp>

#include <algorithm>

namespace vna::application {
namespace {

const display_model::TraceSnapshot* findTrace(
    const StateSnapshot& snapshot,
    display_model::TraceId traceId) {
    const auto trace = std::find_if(
        snapshot.display.traces.cbegin(),
        snapshot.display.traces.cend(),
        [traceId](const display_model::TraceSnapshot& candidate) {
            return candidate.id == traceId;
        });
    return trace == snapshot.display.traces.cend() ? nullptr : &*trace;
}

TraceDisplayFrameQueryOutcome resolveFrame(
    const StateSnapshot& snapshot,
    display_model::TraceId traceId,
    const TraceDisplayFrameHandle& frame) {
    const auto* trace = findTrace(snapshot, traceId);
    if (trace == nullptr) {
        return TraceDisplayFrameQueryError{
            .code = TraceDisplayFrameQueryErrorCode::TraceNotFound};
    }
    if (frame == nullptr || frame->traceId != traceId ||
        trace->format != display_model::TraceFormat::LogMagnitude ||
        frame->format != trace->format) {
        return TraceDisplayFrameQueryError{
            .code = TraceDisplayFrameQueryErrorCode::FrameNotAvailable};
    }
    return frame;
}

}  // namespace

TraceDisplayFrameQuery::TraceDisplayFrameQuery(
    const CommandBus& commandBus,
    const TraceDisplayFrameRepository& repository)
    : commandBus_(commandBus), repository_(repository) {}

TraceDisplayFrameQueryOutcome TraceDisplayFrameQuery::latest(
    display_model::TraceId traceId) const {
    const auto snapshot = commandBus_.snapshot();
    return resolveFrame(snapshot, traceId, repository_.latest(traceId));
}

TraceDisplayFrameQueryOutcome TraceDisplayFrameQuery::waitForNext(
    display_model::TraceId traceId,
    std::uint64_t afterSequence,
    std::stop_token token) const {
    const auto before = commandBus_.snapshot();
    const auto* trace = findTrace(before, traceId);
    if (trace == nullptr) {
        return TraceDisplayFrameQueryError{
            .code = TraceDisplayFrameQueryErrorCode::TraceNotFound};
    }
    if (trace->format != display_model::TraceFormat::LogMagnitude) {
        return TraceDisplayFrameQueryError{
            .code = TraceDisplayFrameQueryErrorCode::FrameNotAvailable};
    }
    // snapshot() has returned by this point, so no CommandBus lock overlaps
    // the blocking repository call. A second value snapshot closes the race
    // with control-plane deletion or format changes while the wait was active.
    const auto frame = repository_.waitForNext(traceId, afterSequence, token);
    return resolveFrame(commandBus_.snapshot(), traceId, frame);
}

}  // namespace vna::application
