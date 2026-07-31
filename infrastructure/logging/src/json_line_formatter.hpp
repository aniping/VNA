#pragma once

#include <chrono>
#include <string>

#include <vna/observability/logger.hpp>

namespace vna::logging {

std::string formatJsonLine(
    const observability::LogEvent& event,
    std::chrono::system_clock::time_point timestamp);

}  // namespace vna::logging
