#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vna::web_api::detail {

enum class WebAssetPathKind {
    Missing,
    Regular,
    Directory,
    Unsafe,
};

// Classifies the entry itself without following a link or reparse point.
[[nodiscard]] WebAssetPathKind classifyWebAssetPathNoFollow(
    const std::filesystem::path& path);

[[nodiscard]] std::optional<std::filesystem::path> validateWebRoot(
    const std::optional<std::filesystem::path>& webRoot);
[[nodiscard]] std::optional<std::string> resolveWebAsset(
    const std::filesystem::path& assetsRoot,
    const std::string& requestedPath);

}  // namespace vna::web_api::detail
