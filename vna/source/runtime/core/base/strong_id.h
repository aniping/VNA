#pragma once

#include <cstdint>

namespace vna::core {

/// 为不同领域标识符提供编译期类型隔离，避免把数值相同但语义不同的 ID 混用。
/// @tparam Tag 仅用于区分 ID 类型的空标签类型。
template <typename Tag>
class StrongId final {
public:
    using ValueType = std::uint64_t;

    constexpr StrongId() noexcept = default;
    /// @param value ID 的原始数值；0 表示无效/未设置。
    explicit constexpr StrongId(ValueType value) noexcept : value_(value) {}

    /// @return ID 的原始无符号整数值。
    constexpr ValueType value() const noexcept { return value_; }
    /// @return 原始值非 0 时返回 true。
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

/// 用于绑定配置、授权和数据清单的轻量摘要值。
/// @note 当前只承载摘要数值和有效性，不实现哈希计算或密码学保证。
struct StrongDigest final {
    std::uint64_t value{0U};

    /// @return 摘要非 0 时返回 true。
    constexpr bool valid() const noexcept { return value != 0U; }

    friend constexpr bool operator==(StrongDigest lhs, StrongDigest rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(StrongDigest lhs, StrongDigest rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace vna::core
