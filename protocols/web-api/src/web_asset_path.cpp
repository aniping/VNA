#include "web_asset_path.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace vna::web_api::detail {
namespace {

bool isSafeDirectoryChain(const std::filesystem::path& path) {
    auto current = path.root_path();
    if (classifyWebAssetPathNoFollow(current) != WebAssetPathKind::Directory) {
        return false;
    }
    for (const auto& component : path.relative_path()) {
        current /= component;
        if (classifyWebAssetPathNoFollow(current) !=
            WebAssetPathKind::Directory) {
            return false;
        }
    }
    return true;
}

bool hasExpectedKinds(
    const std::filesystem::path& root,
    const std::filesystem::path& relative) {
    auto current = root;
    for (auto part = relative.begin(); part != relative.end(); ++part) {
        if (*part == "." || *part == "..") {
            return false;
        }
        current /= *part;
        const auto expected = std::next(part) == relative.end()
            ? WebAssetPathKind::Regular
            : WebAssetPathKind::Directory;
        if (classifyWebAssetPathNoFollow(current) != expected) {
            return false;
        }
    }
    return !relative.empty();
}

}  // namespace

std::optional<std::filesystem::path> validateWebRoot(
    const std::optional<std::filesystem::path>& webRoot) {
    if (!webRoot) {
        return std::nullopt;
    }
    const auto root = std::filesystem::absolute(*webRoot).lexically_normal();
    if (!isSafeDirectoryChain(root)) {
        throw std::invalid_argument{"web root must be a directory"};
    }
    if (classifyWebAssetPathNoFollow(root / "index.html") !=
        WebAssetPathKind::Regular) {
        throw std::invalid_argument{"web root must contain regular index.html"};
    }
    if (classifyWebAssetPathNoFollow(root / "assets") !=
        WebAssetPathKind::Directory) {
        throw std::invalid_argument{"web root must contain assets directory"};
    }
    return root;
}

std::optional<std::string> resolveWebAsset(
    const std::filesystem::path& assetsRoot,
    const std::string& requestedPath) {
    if (requestedPath.empty() || requestedPath.find('\\') != std::string::npos) {
        return std::nullopt;
    }
    const auto relative = std::filesystem::path{std::u8string{
        reinterpret_cast<const char8_t*>(requestedPath.data()),
        requestedPath.size()}};
    if (relative.has_root_path() || !hasExpectedKinds(assetsRoot, relative)) {
        return std::nullopt;
    }
    const auto candidate = std::filesystem::canonical(assetsRoot / relative);
    const auto mismatch = std::mismatch(
        assetsRoot.begin(), assetsRoot.end(),
        candidate.begin(), candidate.end());
    // Canonical containment backs up the no-follow component walk. It also
    // keeps Linux path normalization from turning a request into an escape.
    if (mismatch.first != assetsRoot.end() ||
        classifyWebAssetPathNoFollow(candidate) != WebAssetPathKind::Regular) {
        return std::nullopt;
    }
    const auto encoded = candidate.u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

}  // namespace vna::web_api::detail
