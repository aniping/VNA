#include "web_asset_path.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <system_error>

namespace vna::web_api::detail {

WebAssetPathKind classifyWebAssetPathNoFollow(
    const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return WebAssetPathKind::Missing;
        }
        throw std::system_error{
            static_cast<int>(error),
            std::system_category(),
            "GetFileAttributesW"};
    }
    // Reparse points include symbolic links and junctions. Treat the entry as
    // unsafe before considering its target type so requests never follow it.
    if ((attributes &
         (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0) {
        return WebAssetPathKind::Unsafe;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return WebAssetPathKind::Directory;
    }
    return WebAssetPathKind::Regular;
}

}  // namespace vna::web_api::detail
