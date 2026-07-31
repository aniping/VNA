#include <vna/application/trace_display_frame_repository.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace vna::application {
namespace {

std::optional<TraceDisplayFrameError> validateIdentity(
    const TraceDisplayFrame& frame) {
    if (frame.frameId.value() == 0) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidFrameId};
    }
    if (frame.traceId.value() == 0) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidTraceId};
    }
    if (frame.sequenceNumber == 0) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidSequenceNumber};
    }
    return std::nullopt;
}

std::optional<TraceDisplayFrameError> validatePresentation(
    const TraceDisplayFrame& frame) {
    if (frame.format != display_model::TraceFormat::LogMagnitude) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::UnsupportedFormat};
    }
    if (frame.valueUnit != display_model::ScaleUnit::Decibel) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::UnsupportedValueUnit};
    }
    return std::nullopt;
}

std::optional<TraceDisplayFrameError> validateShape(
    const TraceDisplayFrame& frame) {
    const auto points = frame.frequenciesHz.size();
    if (points < 2 || points > frames::kMaxSweepPoints) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidPointCount};
    }
    if (frame.values.size() != points) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::SampleCountMismatch};
    }
    return std::nullopt;
}

std::optional<TraceDisplayFrameError> validateValues(
    const TraceDisplayFrame& frame) {
    const auto finite = [](double value) { return std::isfinite(value); };
    if (!std::all_of(
            frame.frequenciesHz.cbegin(), frame.frequenciesHz.cend(), finite) ||
        !std::all_of(frame.values.cbegin(), frame.values.cend(), finite)) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::NonFiniteValue};
    }
    const auto unordered = std::adjacent_find(
        frame.frequenciesHz.cbegin(),
        frame.frequenciesHz.cend(),
        [](double left, double right) { return right <= left; });
    if (unordered != frame.frequenciesHz.cend()) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::FrequencyNotStrictlyIncreasing};
    }
    return std::nullopt;
}

}  // namespace

TraceDisplayFrameResult::TraceDisplayFrameResult(
    TraceDisplayFrameHandle value)
    : value_(std::move(value)) {}

TraceDisplayFrameResult::TraceDisplayFrameResult(
    TraceDisplayFrameError error)
    : value_(error) {}

bool TraceDisplayFrameResult::hasValue() const noexcept {
    return std::holds_alternative<TraceDisplayFrameHandle>(value_);
}

const TraceDisplayFrameHandle& TraceDisplayFrameResult::value() const {
    return std::get<TraceDisplayFrameHandle>(value_);
}

const TraceDisplayFrameError& TraceDisplayFrameResult::error() const {
    return std::get<TraceDisplayFrameError>(value_);
}

TraceDisplayFrameRepository::TraceDisplayFrameRepository(std::size_t capacity)
    : capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument{"frame repository capacity must be positive"};
    }
}

TraceDisplayFrameResult TraceDisplayFrameRepository::publish(
    TraceDisplayFrame frame) {
    if (const auto error = validateIdentity(frame)) {
        return TraceDisplayFrameResult{*error};
    }
    if (const auto error = validatePresentation(frame)) {
        return TraceDisplayFrameResult{*error};
    }
    if (const auto error = validateShape(frame)) {
        return TraceDisplayFrameResult{*error};
    }
    if (const auto error = validateValues(frame)) {
        return TraceDisplayFrameResult{*error};
    }
    std::lock_guard lock{mutex_};
    const auto found = latestByTrace_.find(frame.traceId.value());
    if (found != latestByTrace_.end() &&
        found->second->frameId == frame.frameId &&
        found->second->sequenceNumber == frame.sequenceNumber) {
        return TraceDisplayFrameResult{found->second};
    }
    // Sequence comparison and replacement share this lock. Every rejection
    // returns before allocation or assignment, preserving the current frame.
    if (found != latestByTrace_.end() &&
        frame.sequenceNumber <= found->second->sequenceNumber) {
        return TraceDisplayFrameResult{TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::SequenceRegression}};
    }
    if (found == latestByTrace_.end() && latestByTrace_.size() >= capacity_) {
        return TraceDisplayFrameResult{TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::CapacityExceeded}};
    }
    auto published = std::make_shared<const TraceDisplayFrame>(std::move(frame));
    latestByTrace_[published->traceId.value()] = published;
    return TraceDisplayFrameResult{std::move(published)};
}

TraceDisplayFrameHandle TraceDisplayFrameRepository::latest(
    display_model::TraceId traceId) const {
    std::lock_guard lock{mutex_};
    const auto found = latestByTrace_.find(traceId.value());
    if (found == latestByTrace_.cend()) {
        return nullptr;
    }
    return found->second;
}

}  // namespace vna::application
