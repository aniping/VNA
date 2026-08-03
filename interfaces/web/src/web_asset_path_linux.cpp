#include "web_asset_path.hpp"

namespace vna::web_api::detail {

WebAssetPathKind classifyWebAssetPathNoFollow(
    const vna::compat::filesystem::path& path) {
    std::error_code error;
    const auto status = vna::compat::filesystem::symlink_status(path, error);
    if (status.type() == vna::compat::filesystem::file_type::not_found) {
        return WebAssetPathKind::Missing;
    }
    if (error) {
        throw vna::compat::filesystem::filesystem_error{
            "symlink_status", path, error};
    }
    if (vna::compat::filesystem::is_regular_file(status)) {
        return WebAssetPathKind::Regular;
    }
    if (vna::compat::filesystem::is_directory(status)) {
        return WebAssetPathKind::Directory;
    }
    return WebAssetPathKind::Unsafe;
}

}  // namespace vna::web_api::detail
