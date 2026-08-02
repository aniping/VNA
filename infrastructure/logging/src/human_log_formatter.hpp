#pragma once

#include <memory>

namespace spdlog {
class formatter;
}

namespace vna::logging {

std::unique_ptr<spdlog::formatter> makeHumanLogFormatter();

}  // namespace vna::logging
