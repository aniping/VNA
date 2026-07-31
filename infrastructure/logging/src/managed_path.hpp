#pragma once

#include <filesystem>

namespace vna::logging {

enum class ManagedPathKind {
    Missing,
    Regular,
    Unsafe,
};

ManagedPathKind classifyManagedPathNoFollow(
    const std::filesystem::path& path);

}  // namespace vna::logging
