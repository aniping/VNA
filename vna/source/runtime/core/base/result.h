#pragma once

#include <utility>
#include <variant>

namespace vna::core {

template <typename T, typename E>
class Result final {
public:
    static Result success(T value) noexcept {
        return Result(std::in_place_index<0>, std::move(value));
    }

    static Result failure(E error) noexcept {
        return Result(std::in_place_index<1>, std::move(error));
    }

    Result(Result&&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;
    Result(const Result&) = default;
    Result& operator=(const Result&) = default;

    bool has_value() const noexcept { return value_.index() == 0U; }
    explicit operator bool() const noexcept { return has_value(); }

    T& value() & noexcept { return std::get<0>(value_); }
    const T& value() const& noexcept { return std::get<0>(value_); }
    T&& take_value() && noexcept { return std::get<0>(std::move(value_)); }

    E& error() & noexcept { return std::get<1>(value_); }
    const E& error() const& noexcept { return std::get<1>(value_); }

private:
    template <std::size_t Index, typename U>
    explicit Result(std::in_place_index_t<Index> index, U&& value) noexcept
        : value_(index, std::forward<U>(value)) {}

    std::variant<T, E> value_;
};

}  // namespace vna::core
