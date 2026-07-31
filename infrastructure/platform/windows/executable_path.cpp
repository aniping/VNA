#include <vna/platform/executable_path.hpp>

#include <windows.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace vna::platform {

std::filesystem::path currentExecutablePath() {
    constexpr std::size_t maximumPathCharacters = 32'768;
    std::vector<wchar_t> buffer(260);

    while (true) {
        const auto length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "resolve executable path");
        }
        if (length < buffer.size()) {
            auto result = std::filesystem::path{
                std::wstring_view{buffer.data(), length}};
            if (!result.is_absolute()) {
                throw std::runtime_error("executable path is not absolute");
            }
            return result;
        }

        if (buffer.size() == maximumPathCharacters) {
            break;
        }
        // Clamp the final retry to Windows' path limit. A plain doubling step
        // would jump from 16640 to 33280 and accidentally skip valid paths.
        buffer.resize(std::min(
            maximumPathCharacters, buffer.size() * 2));
    }

    throw std::runtime_error("executable path exceeds platform limit");
}

}  // namespace vna::platform
