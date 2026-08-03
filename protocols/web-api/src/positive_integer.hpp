#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace vna::web_api::detail {

inline std::optional<std::uint64_t> parsePositiveInteger(
    std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value{};
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        const auto limit = std::numeric_limits<std::uint64_t>::max();
        if (value > (limit - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value == 0 ? std::nullopt
                      : std::optional<std::uint64_t>{value};
}

}  // namespace vna::web_api::detail
