#pragma once

#include <optional>
#include <string>

#include <vna/compat/filesystem.hpp>

namespace vna::web_api::detail {

enum class WebAssetPathKind {
    Missing,
    Regular,
    Directory,
    Unsafe,
};

// Classifies the entry itself without following a link or reparse point.
[[nodiscard]] WebAssetPathKind classifyWebAssetPathNoFollow(
    const vna::compat::filesystem::path& path);

[[nodiscard]] std::optional<vna::compat::filesystem::path> validateWebRoot(
    const std::optional<vna::compat::filesystem::path>& webRoot);
[[nodiscard]] std::optional<std::string> resolveWebAsset(
    const vna::compat::filesystem::path& assetsRoot,
    const std::string& requestedPath);

}  // namespace vna::web_api::detail
