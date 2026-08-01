#pragma once

#include <cstddef>
#include <string>

#include <vna/application/trace_display_frame.hpp>

namespace vna::web_api::detail {

inline constexpr std::size_t maximumDisplayFrameResponseBytes = 131'072;

[[nodiscard]] std::string encodeDisplayFrame(
    const application::TraceDisplayFrame& frame);

}  // namespace vna::web_api::detail
