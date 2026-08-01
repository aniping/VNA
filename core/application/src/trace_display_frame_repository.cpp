#include <vna/application/trace_display_frame_repository.hpp>

#include "trace_display_frame_validation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vna::application {
namespace {

bool isSupportedMeasurementType(domain::MeasurementType type) {
    switch (type) {
        case domain::MeasurementType::S11:
        case domain::MeasurementType::S21:
        case domain::MeasurementType::S12:
        case domain::MeasurementType::S22:
            return true;
    }
    return false;
}

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
    if (frame.measurementId.value() == 0) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidMeasurementId};
    }
    if (!isSupportedMeasurementType(frame.measurementType)) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidMeasurementType};
    }
    if (frame.generation == 0) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidGeneration};
    }
    if (frame.sequenceNumber == 0) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidSequenceNumber};
    }
    return std::nullopt;
}

std::optional<TraceDisplayFrameError> validatePresentation(
    const TraceDisplayFrame& frame) {
    const auto* cartesian =
        std::get_if<CartesianTraceDisplaySamples>(&frame.samples);
    const auto* complex =
        std::get_if<ComplexTraceDisplaySamples>(&frame.samples);
    if (frame.format == display_model::TraceFormat::LogMagnitude &&
        cartesian != nullptr) {
        return cartesian->unit == TraceDisplayUnit::Decibel
            ? std::nullopt
            : std::optional{TraceDisplayFrameError{
                  .code = TraceDisplayFrameErrorCode::UnsupportedValueUnit}};
    }
    if (frame.format == display_model::TraceFormat::Phase &&
        cartesian != nullptr) {
        return cartesian->unit == TraceDisplayUnit::Degree
            ? std::nullopt
            : std::optional{TraceDisplayFrameError{
                  .code = TraceDisplayFrameErrorCode::UnsupportedValueUnit}};
    }
    if (frame.format == display_model::TraceFormat::Smith && complex != nullptr) {
        return complex->unit == TraceDisplayUnit::Unitless
            ? std::nullopt
            : std::optional{TraceDisplayFrameError{
                  .code = TraceDisplayFrameErrorCode::UnsupportedValueUnit}};
    }
    if (frame.format == display_model::TraceFormat::LogMagnitude ||
        frame.format == display_model::TraceFormat::Phase ||
        frame.format == display_model::TraceFormat::Smith) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::SamplePayloadMismatch};
    }
    return TraceDisplayFrameError{
        .code = TraceDisplayFrameErrorCode::UnsupportedFormat};
}

std::size_t sampleCount(const TraceDisplaySamples& samples) {
    return std::visit(
        [](const auto& payload) { return payload.values.size(); }, samples);
}

std::optional<TraceDisplayFrameError> validateShape(
    const TraceDisplayFrame& frame) {
    const auto points = frame.frequenciesHz.size();
    if (points < 2 || points > frames::kMaxSweepPoints) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::InvalidPointCount};
    }
    if (sampleCount(frame.samples) != points) {
        return TraceDisplayFrameError{
            .code = TraceDisplayFrameErrorCode::SampleCountMismatch};
    }
    return std::nullopt;
}

std::optional<TraceDisplayFrameError> validateValues(
    const TraceDisplayFrame& frame) {
    const auto finite = [](double value) { return std::isfinite(value); };
    const auto finiteSamples = std::visit(
        [finite](const auto& payload) {
            return std::all_of(
                payload.values.cbegin(),
                payload.values.cend(),
                [finite](const auto& value) {
                    if constexpr (std::is_same_v<decltype(value), const double&>) {
                        return finite(value);
                    } else {
                        return finite(value.real) && finite(value.imaginary);
                    }
                });
        },
        frame.samples);
    if (!std::all_of(
            frame.frequenciesHz.cbegin(), frame.frequenciesHz.cend(), finite) ||
        !finiteSamples) {
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

namespace internal {

std::optional<TraceDisplayFrameError> validateTraceDisplayFrame(
    const TraceDisplayFrame& frame) {
    if (const auto error = validateIdentity(frame)) {
        return error;
    }
    if (const auto error = validatePresentation(frame)) {
        return error;
    }
    if (const auto error = validateShape(frame)) {
        return error;
    }
    return validateValues(frame);
}

}  // namespace internal

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
    if (const auto error = internal::validateTraceDisplayFrame(frame)) {
        return TraceDisplayFrameResult{*error};
    }
    TraceDisplayFrameHandle published;
    std::shared_ptr<WaitState> waitState;
    {
        std::lock_guard lock{mutex_};
        const auto found = latestByTrace_.find(frame.traceId.value());
        if (found != latestByTrace_.end() &&
            found->second->frameId == frame.frameId &&
            found->second->sequenceNumber == frame.sequenceNumber) {
            return TraceDisplayFrameResult{found->second};
        }
        // Rejection precedes assignment, so failure cannot wake a waiter or
        // replace the last known-good frame.
        if (found != latestByTrace_.end() &&
            frame.sequenceNumber <= found->second->sequenceNumber) {
            return TraceDisplayFrameResult{TraceDisplayFrameError{
                .code = TraceDisplayFrameErrorCode::SequenceRegression}};
        }
        if (found == latestByTrace_.end() &&
            latestByTrace_.size() >= capacity_) {
            return TraceDisplayFrameResult{TraceDisplayFrameError{
                .code = TraceDisplayFrameErrorCode::CapacityExceeded}};
        }
        published =
            std::make_shared<const TraceDisplayFrame>(std::move(frame));
        latestByTrace_[published->traceId.value()] = published;
        const auto waiting = waitStates_.find(published->traceId.value());
        if (waiting != waitStates_.end()) {
            waitState = waiting->second;
        }
    }
    if (waitState != nullptr) {
        waitState->changed.notify_all();
    }
    return TraceDisplayFrameResult{published};
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
