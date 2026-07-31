#include "web_asset_path.hpp"

namespace vna::web_api::detail {

WebAssetPathKind classifyWebAssetPathNoFollow(
    const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) {
        return WebAssetPathKind::Missing;
    }
    if (error) {
        throw std::filesystem::filesystem_error{
            "symlink_status", path, error};
    }
    if (std::filesystem::is_regular_file(status)) {
        return WebAssetPathKind::Regular;
    }
    if (std::filesystem::is_directory(status)) {
        return WebAssetPathKind::Directory;
    }
    return WebAssetPathKind::Unsafe;
}

}  // namespace vna::web_api::detail
