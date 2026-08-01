#pragma once

#include <optional>

#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::application::internal {

[[nodiscard]] std::optional<TraceDisplayFrameError>
validateTraceDisplayFrame(const TraceDisplayFrame& frame);

}  // namespace vna::application::internal
