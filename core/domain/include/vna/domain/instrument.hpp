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
using TraceId = EntityId<struct TraceIdTag>;
using WindowId = EntityId<struct WindowIdTag>;

enum class DomainErrorCode {
    InvalidSweepSettings,
    ChannelNotFound,
    MeasurementNotFound,
    WindowNotFound,
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

enum class MeasurementType {
    S11,
    S21,
};

enum class TraceFormat {
    LogMagnitude,
    Phase,
    Smith,
};

struct ChannelSnapshot {
    ChannelId id;
    SweepSettings sweep;
};

struct MeasurementSnapshot {
    MeasurementId id;
    ChannelId channelId;
    MeasurementType type;
};

struct WindowSnapshot {
    WindowId id;
};

struct TraceSnapshot {
    TraceId id;
    WindowId windowId;
    MeasurementId measurementId;
    TraceFormat format;
};

struct InstrumentSnapshot {
    std::vector<ChannelSnapshot> channels;
    std::vector<MeasurementSnapshot> measurements;
    std::vector<WindowSnapshot> windows;
    std::vector<TraceSnapshot> traces;
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
    [[nodiscard]] WindowId createWindow();
    [[nodiscard]] Result<TraceId> createTrace(
        WindowId windowId,
        MeasurementId measurementId,
        TraceFormat format);
    [[nodiscard]] bool updateTraceFormat(
        TraceId traceId,
        TraceFormat format);
    [[nodiscard]] bool removeTrace(TraceId traceId);
    [[nodiscard]] InstrumentSnapshot snapshot() const;

private:
    std::uint64_t nextChannelId_{1};
    std::uint64_t nextMeasurementId_{1};
    std::uint64_t nextWindowId_{1};
    std::uint64_t nextTraceId_{1};
    InstrumentSnapshot state_;
};

}  // namespace vna::domain
