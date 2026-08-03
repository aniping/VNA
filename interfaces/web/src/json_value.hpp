#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace vna::web_api::detail {

template <typename UInt>
UInt unsignedInteger(const nlohmann::json& object, const char* field) {
    static_assert(std::is_unsigned_v<UInt>);
    const auto& value = object.at(field);
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)) {
        throw std::invalid_argument{"expected unsigned integer"};
    }
    const auto raw = value.get<std::uint64_t>();
    if (raw > std::numeric_limits<UInt>::max()) {
        throw std::invalid_argument{"unsigned integer out of range"};
    }
    return static_cast<UInt>(raw);
}

}  // namespace vna::web_api::detail
