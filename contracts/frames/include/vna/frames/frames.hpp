#pragma once

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

#include <vna/domain/instrument.hpp>

namespace vna::frames {

inline constexpr std::uint32_t kMaxSweepPoints = 2048;

template <typename Tag>
class EntityId {
public:
    explicit constexpr EntityId(std::uint64_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    friend constexpr bool operator==(EntityId, EntityId) = default;

private:
    std::uint64_t value_;
};

using FrameId = EntityId<struct FrameIdTag>;
using SweepId = EntityId<struct SweepIdTag>;
using FrequencyAxisId = EntityId<struct FrequencyAxisIdTag>;

// The coordinator supplies identity and state correlation so acquisition
// backends cannot publish frames detached from the command state they used.
struct FrameContext {
    FrameId frameId;
    SweepId sweepId;
    domain::ChannelId channelId;
    std::uint64_t stateRevision;
    std::uint64_t sequenceNumber;
};

struct FrequencyAxis {
    FrequencyAxisId id;
    std::uint64_t startFrequencyHz;
    std::uint64_t stopFrequencyHz;
    std::uint32_t points;
};

struct ComplexSample {
    double real;
    double imaginary;
};

// A first-port reflection requires only a1 and b1. Additional receiver
// channels belong in later slices when another measurement actually needs them.
struct RawReceiverSample {
    ComplexSample a1;
    ComplexSample b1;
};

// Simulation and hardware sources own payload generation; the coordinator
// attaches FrameContext and the planned axis before publication.
struct RawReceiverPayload {
    std::vector<RawReceiverSample> samples;
};

struct RawReceiverFrame {
    FrameContext context;
    FrequencyAxis frequencyAxis;
    RawReceiverPayload payload;
};

// MeasurementFrame owns synthesized complex values, not display-formatted
// values. The first slice accepts S11 only and makes S21 rejection explicit.
struct MeasurementFrame {
    FrameContext context;
    FrequencyAxis frequencyAxis;
    domain::MeasurementId measurementId;
    domain::MeasurementType type;
    std::vector<ComplexSample> samples;
};

enum class FrameErrorCode {
    InvalidFrameContext,
    InvalidFrequencyAxis,
    PointCountExceeded,
    SampleCountMismatch,
    NonFiniteSample,
    InvalidMeasurementId,
    UnsupportedMeasurementType,
};

struct FrameError {
    FrameErrorCode code;
};

template <typename T>
class Result {
public:
    explicit Result(T value) : value_(std::move(value)) {}
    explicit Result(FrameError error) : value_(error) {}

    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<T>(value_);
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(value_);
    }

    [[nodiscard]] const FrameError& error() const {
        return std::get<FrameError>(value_);
    }

private:
    std::variant<T, FrameError> value_;
};

[[nodiscard]] Result<RawReceiverFrame> makeRawReceiverFrame(
    FrameContext context,
    FrequencyAxis frequencyAxis,
    RawReceiverPayload payload);

[[nodiscard]] Result<MeasurementFrame> makeMeasurementFrame(
    FrameContext context,
    FrequencyAxis frequencyAxis,
    domain::MeasurementId measurementId,
    domain::MeasurementType type,
    std::vector<ComplexSample> samples);

}  // namespace vna::frames
