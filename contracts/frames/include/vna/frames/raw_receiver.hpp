#pragma once

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace vna::frames {
inline constexpr std::uint32_t kMaxSweepPoints = 2048;
inline constexpr std::uint32_t kMaxPortCount = 4;

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

using FrequencyAxisId = EntityId<struct FrequencyAxisIdTag>;

struct FrequencyAxis {
    FrequencyAxisId id;
    std::uint64_t startFrequencyHz;
    std::uint64_t stopFrequencyHz;
    std::uint32_t points;
};

struct ComplexSample {
    double real;
    double imaginary;
    friend bool operator==(const ComplexSample&, const ComplexSample&) = default;
};

// A point keeps the active reference next to every response receiver, allowing
// Sij selection without a separate acquisition path per measurement name.
struct RawReceiverSample {
    ComplexSample reference;
    std::vector<ComplexSample> responses;
    bool operator==(const RawReceiverSample&) const = default;
};

// Port indices are one-based, matching conventional S-parameter notation.
// A plan may acquire unique source-state subsets; each state still carries all
// response ports so later processing can share the frame.
struct RawSourceState {
    std::uint32_t sourcePort;
    std::vector<RawReceiverSample> samples;
    bool operator==(const RawSourceState&) const = default;
};

// Payload deliberately carries no Channel, Trace, or state revision. A
// continuous owner can publish independently; an application attaches its
// correlation only when needed.
struct RawReceiverPayload {
    std::uint32_t portCount;
    std::vector<RawSourceState> sourceStates;
    bool operator==(const RawReceiverPayload&) const = default;
};

enum class FrameErrorCode {
    InvalidFrameContext,
    InvalidFrequencyAxis,
    PointCountExceeded,
    InvalidPortCount,
    InvalidSourcePort,
    DuplicateSourcePort,
    SampleCountMismatch,
    ResponseCountMismatch,
    NonFiniteSample,
    InvalidMeasurementId,
    MeasurementChannelMismatch,
    ZeroReference,
    NonFiniteTraceValue,
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

}  // namespace vna::frames
