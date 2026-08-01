#pragma once

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace vna::domain {

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

using ChannelId = EntityId<struct ChannelIdTag>;
using MeasurementId = EntityId<struct MeasurementIdTag>;

enum class DomainErrorCode {
    InvalidSweepSettings,
    ChannelNotFound,
    MeasurementNotFound,
};

struct DomainError {
    DomainErrorCode code;
};

template <typename T>
class Result {
public:
    explicit Result(T value) : value_(std::move(value)) {}
    explicit Result(DomainError error) : value_(error) {}

    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<T>(value_);
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(value_);
    }

    [[nodiscard]] const DomainError& error() const {
        return std::get<DomainError>(value_);
    }

private:
    std::variant<T, DomainError> value_;
};

struct SweepSettings {
    std::uint64_t startFrequencyHz;
    std::uint64_t stopFrequencyHz;
    std::uint32_t points;
    std::uint64_t ifBandwidthHz;
    double powerDbm;
};

// These enums describe the Channel's configured sweep behavior. They do not
// report whether the acquisition worker is running, healthy, or producing a
// frame; those runtime facts belong to the acquisition lifecycle.
enum class SweepMode {
    Continuous,
};

enum class TriggerSource {
    None,
};

enum class MeasurementType {
    S11,
    S21,
};

struct ChannelSnapshot {
    ChannelId id;
    SweepSettings sweep;
    SweepMode sweepMode{SweepMode::Continuous};
    TriggerSource triggerSource{TriggerSource::None};
};

struct MeasurementSnapshot {
    MeasurementId id;
    ChannelId channelId;
    MeasurementType type;
};

struct InstrumentSnapshot {
    std::vector<ChannelSnapshot> channels;
    std::vector<MeasurementSnapshot> measurements;
};

class Instrument {
public:
    [[nodiscard]] Result<ChannelId> createChannel(SweepSettings settings);
    [[nodiscard]] Result<ChannelId> updateChannelSweep(
        ChannelId channelId,
        SweepSettings settings);
    [[nodiscard]] Result<MeasurementId> createMeasurement(
        ChannelId channelId,
        MeasurementType type);
    [[nodiscard]] bool containsMeasurement(MeasurementId measurementId) const;
    [[nodiscard]] InstrumentSnapshot snapshot() const;

private:
    std::uint64_t nextChannelId_{1};
    std::uint64_t nextMeasurementId_{1};
    InstrumentSnapshot state_;
};

}  // namespace vna::domain
