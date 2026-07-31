#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>
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
    std::stop_token)>;

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
    std::variant<OperationSnapshot, SingleSweepSubmitError>;

class SingleSweepExecutor {
public:
    SingleSweepExecutor(
        std::size_t queueCapacity,
        RawSweepSource source,
        OperationManager& operations,
        TraceDisplayFrameRepository& frames);
    ~SingleSweepExecutor();

    SingleSweepExecutor(const SingleSweepExecutor&) = delete;
    SingleSweepExecutor& operator=(const SingleSweepExecutor&) = delete;
    SingleSweepExecutor(SingleSweepExecutor&&) = delete;
    SingleSweepExecutor& operator=(SingleSweepExecutor&&) = delete;

    // Admission and Operation creation share one executor lock. A rejected item
    // therefore never leaves an Operation that no worker can own.
    [[nodiscard]] SingleSweepSubmitResult submit(SingleSweepWorkItem work);

    // stop is idempotent and joins the worker. Dependencies must outlive this
    // executor, and source implementations must release promptly on stop.
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna::application
