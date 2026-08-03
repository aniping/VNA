#pragma once

#include <cstdint>

namespace vna::application {

// OperationId is shared by command acceptance and lifecycle observation. Its
// standalone header avoids coupling either contract to the other's full API.
class OperationId {
public:
    explicit constexpr OperationId(std::uint64_t value) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    friend constexpr bool operator==(OperationId left, OperationId right) {
        return left.value_ == right.value_;
    }
    friend constexpr bool operator!=(OperationId left, OperationId right) {
        return !(left == right);
    }

private:
    std::uint64_t value_;
};

}  // namespace vna::application
