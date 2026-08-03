#pragma once

#include <cstddef>
#include <string>

#include <vna/application/trace_display_frame.hpp>
#include <vna/application/trace_display_frame_set.hpp>
#include <vna/application/sweep_preview_exchange.hpp>

namespace vna::web_api::detail {

inline constexpr std::size_t maximumDisplayFrameResponseBytes = 131'072;
// A complete four-Trace, 2048-point mixed-format set duplicates the shared
// frequency axis by the frozen wire contract and therefore exceeds REST's
// single-frame boundary. Keep the larger limit private to this WS message.
inline constexpr std::size_t maximumDisplayStreamMessageBytes = 1'048'576;

[[nodiscard]] std::string encodeDisplayFrame(
    const application::TraceDisplayFrame& frame);
[[nodiscard]] std::string encodeDisplayFrameSet(
    const application::TraceDisplayFrameSet& frameSet);
[[nodiscard]] std::string encodeSweepPreviewEvent(
    const application::SweepPreviewEvent& event);

}  // namespace vna::web_api::detail
