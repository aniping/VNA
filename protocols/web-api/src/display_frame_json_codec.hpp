#pragma once

#include <string>

#include <vna/application/trace_display_frame.hpp>

namespace vna::web_api::detail {

[[nodiscard]] std::string encodeDisplayFrame(
    const application::TraceDisplayFrame& frame);

}  // namespace vna::web_api::detail
