#include <vna/platform/executable_path.hpp>

#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace vna::platform {

vna::compat::filesystem::path currentExecutablePath() {
    constexpr std::size_t maximumPathCharacters = 1U << 20U;
    std::vector<char> buffer(256);

    while (buffer.size() <= maximumPathCharacters) {
        const auto length = readlink(
            "/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            throw std::system_error(
                errno, std::generic_category(), "read /proc/self/exe");
        }
        if (static_cast<std::size_t>(length) < buffer.size()) {
            auto result = vna::compat::filesystem::path{
                std::string_view{buffer.data(), static_cast<std::size_t>(length)}};
            if (!result.is_absolute()) {
                throw std::runtime_error("executable path is not absolute");
            }
            return result;
        }

        // readlink does not append a terminator and returns the buffer size on
        // truncation, so retry with a larger owned buffer.
        buffer.resize(buffer.size() * 2);
    }

    throw std::runtime_error("executable path exceeds platform limit");
}

}  // namespace vna::platform
