#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include <vna/display_model/display_workspace.hpp>
#include <vna/frames/frames.hpp>

namespace vna::application {

// Units live beside their matching sample payload so an application frame
// cannot accidentally pair scalar and complex data through parallel fields.
enum class TraceDisplayUnit {
    Decibel,
    Degree,
    Unitless,
};

struct CartesianTraceDisplaySamples {
    TraceDisplayUnit unit;
    std::vector<double> values;
    friend bool operator==(
        const CartesianTraceDisplaySamples& left,
        const CartesianTraceDisplaySamples& right) {
        return left.unit == right.unit && left.values == right.values;
    }
    friend bool operator!=(
        const CartesianTraceDisplaySamples& left,
        const CartesianTraceDisplaySamples& right) {
        return !(left == right);
    }
};

struct ComplexTraceDisplaySamples {
    TraceDisplayUnit unit;
    std::vector<frames::ComplexSample> values;
    friend bool operator==(
        const ComplexTraceDisplaySamples& left,
        const ComplexTraceDisplaySamples& right) {
        return left.unit == right.unit && left.values == right.values;
    }
    friend bool operator!=(
        const ComplexTraceDisplaySamples& left,
        const ComplexTraceDisplaySamples& right) {
        return !(left == right);
    }
};

using TraceDisplaySamples = std::variant<
    CartesianTraceDisplaySamples,
    ComplexTraceDisplaySamples>;

// Once published, this DTO is the immutable handoff consumed by display
// queries. Smith values remain synthesized complex Sij; this boundary neither
// converts them to impedance nor clips them to a unit circle.
struct TraceDisplayFrame {
    frames::FrameId frameId;
    display_model::TraceId traceId;
    domain::MeasurementId measurementId;
    domain::MeasurementType measurementType;
    std::uint64_t stateRevision;
    std::uint64_t generation;
    std::uint64_t sequenceNumber;
    display_model::TraceFormat format;
    std::vector<double> frequenciesHz;
    TraceDisplaySamples samples;
    friend bool operator==(
        const TraceDisplayFrame& left,
        const TraceDisplayFrame& right) {
        return left.frameId == right.frameId && left.traceId == right.traceId &&
            left.measurementId == right.measurementId &&
            left.measurementType == right.measurementType &&
            left.stateRevision == right.stateRevision &&
            left.generation == right.generation &&
            left.sequenceNumber == right.sequenceNumber &&
            left.format == right.format &&
            left.frequenciesHz == right.frequenciesHz &&
            left.samples == right.samples;
    }
    friend bool operator!=(
        const TraceDisplayFrame& left,
        const TraceDisplayFrame& right) {
        return !(left == right);
    }
};

}  // namespace vna::application
