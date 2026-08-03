#pragma once

#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/frames/frames.hpp>

namespace vna::application {

// Long-running work fails after command acceptance, so its terminal reason is
// an application operation concern rather than a CommandError. The code names
// the pipeline stage while the typed cause preserves its originating module.
enum class SingleSweepFailureCode {
    RawSweepFailed,
    RawFrameRejected,
    MeasurementSynthesisFailed,
    LogMagnitudeProjectionFailed,
    FrequencyMaterializationFailed,
    TraceDisplayPublishFailed,
    UnexpectedFailure,
};

// Operation snapshots cross worker, query, and completion-fence boundaries.
// Their copies must therefore remain non-throwing after an Operation becomes
// terminal. std::variant does not expose that guarantee for every toolchain,
// even though these small typed causes themselves never allocate. This wrapper
// makes the stronger application contract explicit while retaining typed
// inspection for diagnostics.
class OperationFailureCause {
public:
    using Value = std::variant<
        std::monostate,
        frames::FrameError,
        TraceDisplayFrameError,
        std::exception_ptr>;

    OperationFailureCause() noexcept = default;
    OperationFailureCause(frames::FrameError cause) noexcept : value_(cause) {}
    OperationFailureCause(TraceDisplayFrameError cause) noexcept
        : value_(cause) {}
    OperationFailureCause(std::exception_ptr cause) noexcept
        : value_(std::move(cause)) {}

    OperationFailureCause(const OperationFailureCause& other) noexcept
        : value_(copyValue(other.value_)) {}
    OperationFailureCause& operator=(
        const OperationFailureCause& other) noexcept {
        if (this != &other) {
            value_ = copyValue(other.value_);
        }
        return *this;
    }
    OperationFailureCause(OperationFailureCause&&) noexcept = default;
    OperationFailureCause& operator=(OperationFailureCause&&) noexcept =
        default;

    template <typename T>
    [[nodiscard]] const T* getIf() const noexcept {
        return std::get_if<T>(&value_);
    }

    template <typename T>
    [[nodiscard]] bool holds() const noexcept {
        return std::holds_alternative<T>(value_);
    }

private:
    static Value copyValue(const Value& source) noexcept {
        return std::visit(
            [](const auto& cause) -> Value { return Value{cause}; }, source);
    }

    Value value_{};
};

static_assert(std::is_nothrow_move_assignable_v<OperationFailureCause::Value>);

struct OperationFailure {
    SingleSweepFailureCode code;
    // The application code is stable for lifecycle consumers, while the cause
    // retains the exact lower-module failure for diagnostics and recovery.
    OperationFailureCause cause{};
};

}  // namespace vna::application
