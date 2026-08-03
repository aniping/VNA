#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

namespace vna::compat {

template <typename Element>
class Span {
public:

    constexpr Span() noexcept = default;
    constexpr Span(Element* data, std::size_t size) noexcept
        : data_{data}, size_{size} {}

    template <typename Container, std::enable_if_t<std::is_convertible_v<
                                      decltype(std::declval<Container&>().data()),
                                      Element*>, int> = 0>
    Span(Container& values) noexcept
        : data_{values.data()}, size_{values.size()} {}

    template <typename Container, std::enable_if_t<std::is_const_v<Element> &&
                                      std::is_convertible_v<decltype(
                                          std::declval<const Container&>().data()),
                                          Element*>, int> = 0>
    Span(const Container& values) noexcept
        : data_{values.data()}, size_{values.size()} {}

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] constexpr Element* begin() const noexcept { return data_; }
    [[nodiscard]] constexpr Element* end() const noexcept { return data_ + size_; }
    [[nodiscard]] constexpr Element& operator[](std::size_t index) const {
        return data_[index];
    }

private:
    Element* data_{};
    std::size_t size_{};
};

}  // namespace vna::compat
