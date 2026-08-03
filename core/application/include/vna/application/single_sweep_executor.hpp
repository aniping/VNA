#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vna/compat/stop_token.hpp>
#include <utility>
#include <variant>

#include <vna/application/operation_manager.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/frames/frames.hpp>

namespace vna::application {

// The application supplies correlation and display ownership; a source only
// acquires receiver payload for a planned axis. This keeps simulation, hardware,
// and replay adapters outside the application module's dependency graph.
using RawSweepSource = std::function<frames::Result<frames::RawReceiverPayload>(
    const frames::FrequencyAxis&,
    vna::compat::StopToken)>;

struct SingleSweepWorkItem {
    CommandId commandId;
    SessionId sessionId;
    frames::FrameContext frameContext;
    frames::FrequencyAxis frequencyAxis;
    domain::MeasurementSnapshot measurement;
    display_model::TraceId traceId;
};

enum class SingleSweepSubmitErrorCode {
    QueueFull,
    Stopped,
};

struct SingleSweepSubmitError {
    SingleSweepSubmitErrorCode code;
};

using SingleSweepSubmitResult =
    std::variant<OperationId, SingleSweepSubmitError>;

class SingleSweepExecution {
public:
    virtual ~SingleSweepExecution() = default;
    [[nodiscard]] virtual SingleSweepSubmitResult submit(
        SingleSweepWorkItem work) = 0;
    virtual void invalidateTraceFrame(
        display_model::TraceId traceId) noexcept = 0;
    virtual void discardTrace(display_model::TraceId traceId) noexcept = 0;
};

class SingleSweepExecutor final : public SingleSweepExecution {
public:
    // The custom publisher seam exposes repository-boundary failures to tests
    // and adapters. It runs while the Trace lifecycle gate is held so publish
    // and retirement are linearized; it must return promptly and must not
    // re-enter this executor, including through discardTrace().
    SingleSweepExecutor(
        std::size_t queueCapacity,
        RawSweepSource source,
        OperationManager& operations,
        TraceDisplayFrameRepository& frames,
        TraceDisplayPublisher publish);
    SingleSweepExecutor(
        std::size_t queueCapacity,
        RawSweepSource source,
        OperationManager& operations,
        TraceDisplayFrameRepository& frames)
        : SingleSweepExecutor(
              queueCapacity,
              std::move(source),
              operations,
              frames,
              [&frames](TraceDisplayFrame frame) {
                  return frames.publish(std::move(frame));
              }) {}
    ~SingleSweepExecutor() override;

    SingleSweepExecutor(const SingleSweepExecutor&) = delete;
    SingleSweepExecutor& operator=(const SingleSweepExecutor&) = delete;
    SingleSweepExecutor(SingleSweepExecutor&&) = delete;
    SingleSweepExecutor& operator=(SingleSweepExecutor&&) = delete;

    // Admission and Operation creation share one executor lock. A rejected item
    // therefore never leaves an Operation that no worker can own.
    [[nodiscard]] SingleSweepSubmitResult submit(
        SingleSweepWorkItem work) override;

    // Invalidation drops only retained display data. Admitted Operations keep
    // their existing lifecycle and may publish a later frame if still valid.
    void invalidateTraceFrame(
        display_model::TraceId traceId) noexcept override;

    // The CommandBus calls this after deleting the Trace while its own lock
    // still prevents later admission. It only requests cancellation; terminal
    // fence delivery stays on the worker, outside application transaction locks.
    void discardTrace(display_model::TraceId traceId) noexcept override;

    // stop is idempotent and joins the worker. Dependencies must outlive this
    // executor, and source implementations must release promptly on stop.
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna::application
