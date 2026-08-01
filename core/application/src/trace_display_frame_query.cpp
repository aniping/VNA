#include <vna/application/trace_display_frame_query.hpp>

#include <algorithm>
#include <optional>

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

const domain::MeasurementSnapshot* findMeasurement(
    const StateSnapshot& snapshot,
    domain::MeasurementId measurementId) {
    const auto measurement = std::find_if(
        snapshot.instrument.measurements.cbegin(),
        snapshot.instrument.measurements.cend(),
        [measurementId](const domain::MeasurementSnapshot& candidate) {
            return candidate.id == measurementId;
        });
    return measurement == snapshot.instrument.measurements.cend()
        ? nullptr
        : &*measurement;
}

struct TraceBinding {
    domain::MeasurementId measurementId;
    domain::MeasurementType measurementType;
    display_model::TraceFormat format;
    friend bool operator==(const TraceBinding&, const TraceBinding&) = default;
};

std::optional<TraceBinding> findBinding(
    const StateSnapshot& snapshot,
    display_model::TraceId traceId) {
    const auto* trace = findTrace(snapshot, traceId);
    if (trace == nullptr) {
        return std::nullopt;
    }
    const auto* measurement = findMeasurement(snapshot, trace->measurementId);
    if (measurement == nullptr) {
        return std::nullopt;
    }
    return TraceBinding{trace->measurementId, measurement->type, trace->format};
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
    const auto binding = findBinding(snapshot, traceId);
    if (!binding.has_value() || frame == nullptr || frame->traceId != traceId ||
        frame->measurementId != binding->measurementId ||
        frame->measurementType != binding->measurementType ||
        frame->format != binding->format) {
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
    const auto binding = findBinding(before, traceId);
    if (!binding.has_value()) {
        return TraceDisplayFrameQueryError{
            .code = TraceDisplayFrameQueryErrorCode::FrameNotAvailable};
    }
    // The repository registers this waiter before invoking validation, and it
    // invokes validation without its mutex. Thus this snapshot cannot nest the
    // two module locks, while a concurrent discard cannot slip between policy
    // validation and waiter registration.
    const auto frame = repository_.waitForNext(
        traceId, afterSequence, token, [this, traceId, binding] {
            const auto current = commandBus_.snapshot();
            return findBinding(current, traceId) == binding;
        });
    return resolveFrame(commandBus_.snapshot(), traceId, frame);
}

}  // namespace vna::application
