#include "sweep_preview_validation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <unordered_set>

namespace vna::application {
namespace {

SweepPreviewError error(SweepPreviewErrorCode code) {
    return SweepPreviewError{code};
}

std::optional<SweepPreviewError> validateMetadata(
    const SweepPreview& preview) {
    if (preview.identity.generation == 0 ||
        preview.identity.sweepId.value() == 0 ||
        preview.channelId.value() == 0) {
        return error(SweepPreviewErrorCode::InvalidIdentity);
    }
    if (preview.sequenceNumber == 0) {
        return error(SweepPreviewErrorCode::InvalidSequenceNumber);
    }
    if (preview.totalPointCount < 2 ||
        preview.totalPointCount > frames::kMaxSweepPoints) {
        return error(SweepPreviewErrorCode::InvalidTotalPointCount);
    }
    return std::nullopt;
}

bool supportedMeasurementType(domain::MeasurementType type) {
    switch (type) {
        case domain::MeasurementType::S11:
        case domain::MeasurementType::S21:
        case domain::MeasurementType::S12:
        case domain::MeasurementType::S22:
            return true;
    }
    return false;
}

std::optional<SweepPreviewError> validatePresentation(
    const SweepTracePreview& trace) {
    const auto* cartesian =
        std::get_if<CartesianTraceDisplaySamples>(&trace.samples);
    const auto* complex =
        std::get_if<ComplexTraceDisplaySamples>(&trace.samples);
    if (trace.format == display_model::TraceFormat::LogMagnitude && cartesian) {
        return cartesian->unit == TraceDisplayUnit::Decibel
            ? std::nullopt
            : std::optional{error(SweepPreviewErrorCode::UnsupportedValueUnit)};
    }
    if (trace.format == display_model::TraceFormat::Phase && cartesian) {
        return cartesian->unit == TraceDisplayUnit::Degree
            ? std::nullopt
            : std::optional{error(SweepPreviewErrorCode::UnsupportedValueUnit)};
    }
    if (trace.format == display_model::TraceFormat::Smith && complex) {
        return complex->unit == TraceDisplayUnit::Unitless
            ? std::nullopt
            : std::optional{error(SweepPreviewErrorCode::UnsupportedValueUnit)};
    }
    if (trace.format == display_model::TraceFormat::LogMagnitude ||
        trace.format == display_model::TraceFormat::Phase ||
        trace.format == display_model::TraceFormat::Smith) {
        return error(SweepPreviewErrorCode::SamplePayloadMismatch);
    }
    return error(SweepPreviewErrorCode::UnsupportedFormat);
}

std::size_t sampleCount(const TraceDisplaySamples& samples) {
    return std::visit(
        [](const auto& payload) { return payload.values.size(); }, samples);
}

bool finiteSamples(const TraceDisplaySamples& samples) {
    return std::visit(
        [](const auto& payload) {
            return std::all_of(
                payload.values.cbegin(), payload.values.cend(),
                [](const auto& value) {
                    if constexpr (std::is_same_v<
                                      std::decay_t<decltype(value)>, double>) {
                        return std::isfinite(value);
                    } else {
                        return std::isfinite(value.real) &&
                               std::isfinite(value.imaginary);
                    }
                });
        },
        samples);
}

std::optional<SweepPreviewError> validateValues(
    const SweepTracePreview& trace,
    std::uint32_t totalPointCount) {
    const auto points = trace.frequenciesHz.size();
    if (points == 0 || points > totalPointCount) {
        return error(SweepPreviewErrorCode::InvalidPrefixLength);
    }
    if (sampleCount(trace.samples) != points) {
        return error(SweepPreviewErrorCode::SampleCountMismatch);
    }
    const auto finiteFrequency = [](double value) {
        return std::isfinite(value);
    };
    if (!std::all_of(
            trace.frequenciesHz.cbegin(), trace.frequenciesHz.cend(),
            finiteFrequency) ||
        !finiteSamples(trace.samples)) {
        return error(SweepPreviewErrorCode::NonFiniteValue);
    }
    const auto unordered = std::adjacent_find(
        trace.frequenciesHz.cbegin(), trace.frequenciesHz.cend(),
        [](double left, double right) { return right <= left; });
    return unordered == trace.frequenciesHz.cend()
        ? std::nullopt
        : std::optional{
              error(SweepPreviewErrorCode::FrequencyNotStrictlyIncreasing)};
}

bool sameSamplePrefix(
    const TraceDisplaySamples& current,
    const TraceDisplaySamples& next) {
    return std::visit(
        [&next](const auto& currentPayload) {
            using Payload = std::decay_t<decltype(currentPayload)>;
            const auto* nextPayload = std::get_if<Payload>(&next);
            return nextPayload != nullptr &&
                   nextPayload->unit == currentPayload.unit &&
                   nextPayload->values.size() >= currentPayload.values.size() &&
                   std::equal(
                       currentPayload.values.cbegin(),
                       currentPayload.values.cend(),
                       nextPayload->values.cbegin());
        },
        current);
}

bool sameTracePrefix(
    const SweepTracePreview& current,
    const SweepTracePreview& next) {
    return current.traceId == next.traceId &&
           current.measurementId == next.measurementId &&
           current.measurementType == next.measurementType &&
           current.format == next.format &&
           next.frequenciesHz.size() >= current.frequenciesHz.size() &&
           std::equal(
               current.frequenciesHz.cbegin(),
               current.frequenciesHz.cend(),
               next.frequenciesHz.cbegin()) &&
           sameSamplePrefix(current.samples, next.samples);
}

}  // namespace

namespace internal {

std::optional<SweepPreviewError> validateSweepPreview(
    const SweepPreview& preview) {
    if (const auto invalid = validateMetadata(preview)) {
        return invalid;
    }
    if (preview.traces.empty()) {
        return error(SweepPreviewErrorCode::EmptyTraceSet);
    }
    std::unordered_set<std::uint64_t> traceIds;
    for (const auto& trace : preview.traces) {
        if (trace.traceId.value() == 0 || trace.measurementId.value() == 0 ||
            !supportedMeasurementType(trace.measurementType)) {
            return error(SweepPreviewErrorCode::InvalidTraceIdentity);
        }
        if (!traceIds.insert(trace.traceId.value()).second) {
            return error(SweepPreviewErrorCode::DuplicateTraceId);
        }
        if (const auto invalid = validatePresentation(trace)) {
            return invalid;
        }
        if (const auto invalid =
                validateValues(trace, preview.totalPointCount)) {
            return invalid;
        }
    }
    return std::nullopt;
}

bool isCumulativeExtension(
    const SweepPreview& current,
    const SweepPreview& next) {
    if (current.identity != next.identity ||
        current.channelId != next.channelId ||
        current.stateRevision != next.stateRevision ||
        current.sequenceNumber != next.sequenceNumber ||
        current.totalPointCount != next.totalPointCount) {
        return false;
    }
    return std::all_of(
        current.traces.cbegin(), current.traces.cend(),
        [&next](const auto& currentTrace) {
            const auto found = std::find_if(
                next.traces.cbegin(), next.traces.cend(),
                [&currentTrace](const auto& candidate) {
                    return candidate.traceId == currentTrace.traceId;
                });
            return found != next.traces.cend() &&
                   sameTracePrefix(currentTrace, *found);
        });
}

}  // namespace internal
}  // namespace vna::application
