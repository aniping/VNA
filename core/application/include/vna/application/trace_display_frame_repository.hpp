#pragma once

#include <cstddef>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
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

using TraceDisplayPublisher =
    std::function<TraceDisplayFrameResult(TraceDisplayFrame)>;

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
    // A waiter observes only its Trace. It receives the newest retained frame,
    // so a slow reader can intentionally skip intermediate display updates.
    // Null means cancellation or a discard linearized after registration.
    [[nodiscard]] TraceDisplayFrameHandle waitForNext(
        display_model::TraceId traceId,
        std::uint64_t afterSequence,
        std::stop_token token = {}) const;
    // Erasing the repository's ownership releases one capacity slot. Readers
    // already holding the immutable shared frame remain valid independently.
    void discard(display_model::TraceId traceId) noexcept;

private:
    struct WaitState {
        std::condition_variable changed;
        std::size_t waiters{0};
        std::uint64_t discardGeneration{0};
    };
    struct WaitRegistration {
        std::shared_ptr<WaitState> state;
        std::uint64_t traceId;
        std::uint64_t afterSequence;
        std::uint64_t discardGeneration;
    };

    [[nodiscard]] TraceDisplayFrameHandle awaitRegistered(
        WaitRegistration registration,
        std::stop_token token) const;
    void cleanWaitState(
        std::uint64_t traceId,
        const std::shared_ptr<WaitState>& state) const;

    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, TraceDisplayFrameHandle> latestByTrace_;
    mutable std::unordered_map<std::uint64_t, std::shared_ptr<WaitState>>
        waitStates_;
};

}  // namespace vna::application
