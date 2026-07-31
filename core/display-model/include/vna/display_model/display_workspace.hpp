#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <vna/domain/instrument.hpp>

namespace vna::display_model {

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

using WindowId = EntityId<struct WindowIdTag>;
using TraceId = EntityId<struct TraceIdTag>;

enum class TraceFormat {
    LogMagnitude,
    Phase,
    Smith,
};

enum class ScaleUnit {
    Decibel,
};

struct CartesianScaleSnapshot {
    double scalePerDivision;
    double referenceValue;
    double referencePosition;
    double minimum;
    double maximum;
    ScaleUnit unit;
};

enum class DisplayErrorCode {
    WindowNotFound,
    TraceNotFound,
    InvalidScalePerDivision,
    ScaleNotSupportedForFormat,
};

struct DisplayError {
    DisplayErrorCode code;
};

template <typename T>
class Result {
public:
    explicit Result(T value) : value_(std::move(value)) {}
    explicit Result(DisplayError error) : value_(error) {}

    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<T>(value_);
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(value_);
    }

    [[nodiscard]] const DisplayError& error() const {
        return std::get<DisplayError>(value_);
    }

private:
    std::variant<T, DisplayError> value_;
};

struct WindowSnapshot {
    WindowId id;
};

struct TraceSnapshot {
    TraceId id;
    WindowId windowId;
    domain::MeasurementId measurementId;
    TraceFormat format;
    std::optional<CartesianScaleSnapshot> scale;
};

struct DisplayWorkspaceSnapshot {
    std::vector<WindowSnapshot> windows;
    std::vector<TraceSnapshot> traces;
};

class DisplayWorkspace {
public:
    [[nodiscard]] WindowId createWindow();
    [[nodiscard]] Result<TraceId> createTrace(
        WindowId windowId,
        domain::MeasurementId measurementId,
        TraceFormat format);
    [[nodiscard]] Result<TraceId> updateTraceFormat(
        TraceId traceId,
        TraceFormat format);
    [[nodiscard]] Result<TraceId> updateTraceScalePerDivision(
        TraceId traceId,
        double scalePerDivision);
    [[nodiscard]] Result<TraceId> removeTrace(TraceId traceId);
    [[nodiscard]] DisplayWorkspaceSnapshot snapshot() const;

private:
    struct CartesianScaleState {
        double scalePerDivision;
        double referenceValue;
        double referencePosition;
    };

    struct TraceState {
        TraceId id;
        WindowId windowId;
        domain::MeasurementId measurementId;
        TraceFormat format;
        std::optional<CartesianScaleState> scale;
    };

    [[nodiscard]] static std::optional<CartesianScaleState> defaultScaleFor(
        TraceFormat format);
    [[nodiscard]] static CartesianScaleSnapshot scaleSnapshot(
        const CartesianScaleState& scale);

    std::uint64_t nextWindowId_{1};
    std::uint64_t nextTraceId_{1};
    std::vector<WindowSnapshot> windows_;
    std::vector<TraceState> traces_;
};

}  // namespace vna::display_model
