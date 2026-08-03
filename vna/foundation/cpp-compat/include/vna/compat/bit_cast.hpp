#pragma once

#include <cstring>
#include <type_traits>

namespace vna::compat {

template <typename To, typename From>
[[nodiscard]] To bitCast(const From& source) noexcept {
    static_assert(sizeof(To) == sizeof(From), "bitCast requires equal sizes");
    static_assert(std::is_trivially_copyable_v<To>, "To must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<From>, "From must be trivially copyable");
    To destination;
    std::memcpy(&destination, &source, sizeof(To));
    return destination;
}

}  // namespace vna::compat
