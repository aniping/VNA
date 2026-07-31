#include "managed_path.hpp"

namespace vna::logging {

ManagedPathKind classifyManagedPathNoFollow(
    const std::filesystem::path& path) {
    const auto status = std::filesystem::symlink_status(path);
    if (status.type() == std::filesystem::file_type::not_found) {
        return ManagedPathKind::Missing;
    }
    return std::filesystem::is_regular_file(status)
        ? ManagedPathKind::Regular
        : ManagedPathKind::Unsafe;
}

}  // namespace vna::logging
