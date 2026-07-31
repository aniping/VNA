#pragma once

#include <utility>
#include <variant>

namespace vna::core {

/// 表示一次不抛异常的成功或失败结果。
///
/// @tparam T 成功时保存的值类型。
/// @tparam E 失败时保存的错误类型。
/// @note 调用 value()/take_value() 前必须确认 has_value() 为 true；
///       调用 error() 前必须确认 has_value() 为 false。
template <typename T, typename E>
class Result final {
public:
    /// 构造成功结果并取得 value 的所有权。
    /// @param value 要保存的成功值。
    /// @return 保存 value 的 Result。
    static Result success(T value) noexcept {
        return Result(std::in_place_index<0>, std::move(value));
    }

    /// 构造失败结果并取得 error 的所有权。
    /// @param error 要保存的错误值。
    /// @return 保存 error 的 Result。
    static Result failure(E error) noexcept {
        return Result(std::in_place_index<1>, std::move(error));
    }

    Result(Result&&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;
    Result(const Result&) = default;
    Result& operator=(const Result&) = default;

    /// @return 成功分支存在时返回 true。
    bool has_value() const noexcept { return value_.index() == 0U; }
    explicit operator bool() const noexcept { return has_value(); }

    /// @return 成功值的可修改引用；Result 必须处于成功分支。引用只在当前
    ///         Result 未被移动、赋值或销毁期间有效。
    T& value() & noexcept { return std::get<0>(value_); }
    /// @return 成功值的只读引用；Result 必须处于成功分支。引用只在当前
    ///         Result 未被移动、赋值或销毁期间有效。
    const T& value() const& noexcept { return std::get<0>(value_); }
    /// 将成功值移出 Result。
    /// @return 指向 Result 内部成功值的右值引用；Result 必须处于成功分支，
    ///         调用者应立即用它完成移动构造或移动赋值。
    T&& take_value() && noexcept { return std::get<0>(std::move(value_)); }

    /// @return 错误值的可修改引用；Result 必须处于失败分支。引用只在当前
    ///         Result 未被移动、赋值或销毁期间有效。
    E& error() & noexcept { return std::get<1>(value_); }
    /// @return 错误值的只读引用；Result 必须处于失败分支。引用只在当前
    ///         Result 未被移动、赋值或销毁期间有效。
    const E& error() const& noexcept { return std::get<1>(value_); }

private:
    template <std::size_t Index, typename U>
    explicit Result(std::in_place_index_t<Index> index, U&& value) noexcept
        : value_(index, std::forward<U>(value)) {}

    std::variant<T, E> value_;
};

}  // namespace vna::core
