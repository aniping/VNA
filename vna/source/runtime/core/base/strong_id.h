#pragma once

#include <cstdint>

namespace vna::core {

template <typename Tag>
class StrongId final {
public:
    using ValueType = std::uint64_t;

    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId(ValueType value) noexcept : value_(value) {}

    constexpr ValueType value() const noexcept { return value_; }
    constexpr bool valid() const noexcept { return value_ != 0U; }

    friend constexpr bool operator==(StrongId lhs, StrongId rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    friend constexpr bool operator!=(StrongId lhs, StrongId rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    ValueType value_{0U};
};

struct StrongDigest final {
    std::uint64_t value{0U};

    constexpr bool valid() const noexcept { return value != 0U; }

    friend constexpr bool operator==(StrongDigest lhs, StrongDigest rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(StrongDigest lhs, StrongDigest rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace vna::core
