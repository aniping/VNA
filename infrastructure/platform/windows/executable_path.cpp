#include <vna/platform/executable_path.hpp>

#include <windows.h>

#include <stdexcept>
#include <string_view>
#include <vector>

namespace vna::platform {

std::filesystem::path currentExecutablePath() {
    constexpr std::size_t maximumPathCharacters = 32'768;
    std::vector<wchar_t> buffer(260);

    while (buffer.size() <= maximumPathCharacters) {
        const auto length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::runtime_error("failed to resolve executable path");
        }
        if (length < buffer.size()) {
            auto result = std::filesystem::path{
                std::wstring_view{buffer.data(), length}};
            if (!result.is_absolute()) {
                throw std::runtime_error("executable path is not absolute");
            }
            return result;
        }

        // GetModuleFileNameW reports the supplied size when the buffer is too
        // small. Grow explicitly so long install roots are not truncated.
        buffer.resize(buffer.size() * 2);
    }

    throw std::runtime_error("executable path exceeds platform limit");
}

}  // namespace vna::platform
