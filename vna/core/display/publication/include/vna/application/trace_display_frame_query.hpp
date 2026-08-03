#pragma once

#include <cstdint>
#include <vna/compat/stop_token.hpp>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::application {

enum class TraceDisplayFrameQueryErrorCode {
    TraceNotFound,
    FrameNotAvailable,
};

struct TraceDisplayFrameQueryError {
    TraceDisplayFrameQueryErrorCode code;
};

using TraceDisplayFrameQueryOutcome = std::variant<
    TraceDisplayFrameHandle,
    TraceDisplayFrameQueryError>;

// Resolves a published frame against the current Trace definition. The query
// takes value snapshots and never holds CommandBus and repository locks at the
// same time, keeping protocol adapters free of control-plane lock ordering.
// Both borrowed dependencies must outlive the query instance.
class TraceDisplayFrameQuery {
public:
    TraceDisplayFrameQuery(
        const CommandBus& commandBus,
        const TraceDisplayFrameRepository& repository);

    [[nodiscard]] TraceDisplayFrameQueryOutcome latest(
        display_model::TraceId traceId) const;
    // Waiting never holds a CommandBus lock. The Trace is checked before and
    // after the repository wait so deletion or format changes cannot expose a
    // frame that no longer belongs to the current display definition.
    [[nodiscard]] TraceDisplayFrameQueryOutcome waitForNext(
        display_model::TraceId traceId,
        std::uint64_t afterSequence,
        vna::compat::StopToken token = {}) const;

private:
    const CommandBus& commandBus_;
    const TraceDisplayFrameRepository& repository_;
};

}  // namespace vna::application
