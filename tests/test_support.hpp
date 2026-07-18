#pragma once

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace vna::test {

inline void require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(
            std::string(file) + ":" + std::to_string(line) +
            " requirement failed: " + expression);
    }
}

template <typename Function>
int run(const char* name, Function&& function) noexcept {
    try {
        function();
        std::cout << "[PASS] " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "[FAIL] " << name << ": unknown exception\n";
        return 1;
    }
}

}  // namespace vna::test

#define VNA_REQUIRE(expression) \
    ::vna::test::require((expression), #expression, __FILE__, __LINE__)
