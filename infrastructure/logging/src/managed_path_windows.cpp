#include "managed_path.hpp"

#include <system_error>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace vna::logging {

ManagedPathKind classifyManagedPathNoFollow(
    const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return ManagedPathKind::Missing;
        }
        throw std::system_error(
            static_cast<int>(error), std::system_category(),
            "failed to inspect managed log path");
    }
    constexpr auto unsafeAttributes =
        FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY |
        FILE_ATTRIBUTE_DEVICE;
    return (attributes & unsafeAttributes) == 0
        ? ManagedPathKind::Regular
        : ManagedPathKind::Unsafe;
}

}  // namespace vna::logging
