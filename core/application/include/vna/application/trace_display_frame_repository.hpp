#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <variant>

#include <vna/application/trace_display_frame.hpp>

namespace vna::application {

enum class TraceDisplayFrameErrorCode {
    InvalidFrameId,
    InvalidTraceId,
    InvalidSequenceNumber,
    InvalidPointCount,
    SampleCountMismatch,
    NonFiniteValue,
    FrequencyNotStrictlyIncreasing,
    UnsupportedFormat,
    UnsupportedValueUnit,
    SequenceRegression,
    CapacityExceeded,
};

struct TraceDisplayFrameError {
    TraceDisplayFrameErrorCode code;
};

using TraceDisplayFrameHandle = std::shared_ptr<const TraceDisplayFrame>;

class TraceDisplayFrameResult {
public:
    explicit TraceDisplayFrameResult(TraceDisplayFrameHandle value);
    explicit TraceDisplayFrameResult(TraceDisplayFrameError error);

    [[nodiscard]] bool hasValue() const noexcept;
    [[nodiscard]] const TraceDisplayFrameHandle& value() const;
    [[nodiscard]] const TraceDisplayFrameError& error() const;

private:
    std::variant<TraceDisplayFrameHandle, TraceDisplayFrameError> value_;
};

class TraceDisplayFrameRepository {
public:
    explicit TraceDisplayFrameRepository(std::size_t capacity);

    // Capacity counts Trace identities, not history: replacing an existing
    // Trace keeps one latest frame and therefore consumes no additional slot.
    // Publish takes ownership by value. A successful call moves that value into
    // immutable shared storage so readers remain valid across later replacement.
    [[nodiscard]] TraceDisplayFrameResult publish(TraceDisplayFrame frame);
    [[nodiscard]] TraceDisplayFrameHandle latest(
        display_model::TraceId traceId) const;
    // Erasing the repository's ownership releases one capacity slot. Readers
    // already holding the immutable shared frame remain valid independently.
    void discard(display_model::TraceId traceId) noexcept;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, TraceDisplayFrameHandle> latestByTrace_;
};

}  // namespace vna::application
