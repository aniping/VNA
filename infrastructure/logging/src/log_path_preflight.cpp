#include "log_path_preflight.hpp"

#include "managed_path.hpp"

#include <spdlog/sinks/rotating_file_sink.h>

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace vna::logging {
namespace {

constexpr std::string_view kActiveFilename = "vna.log.jsonl";
constexpr std::string_view kArchivePrefix = "vna.log.";
constexpr std::string_view kArchiveExtension = ".jsonl";
constexpr std::string_view kLegacyPrefix = "vna.log.jsonl.";

struct ArchiveIdentity {
    std::size_t index;
    bool legacy;
};

std::optional<std::size_t> parseIndex(std::string_view text) {
    if (text.empty() || text.front() == '0') return std::nullopt;
    std::size_t index = 0;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        index == 0) {
        return std::nullopt;
    }
    return index;
}

std::optional<ArchiveIdentity> archiveIdentity(std::string_view name) {
    if (name.rfind(kLegacyPrefix, 0) == 0) {
        const auto index = parseIndex(name.substr(kLegacyPrefix.size()));
        if (index) return ArchiveIdentity{*index, true};
    }
    if (name.rfind(kArchivePrefix, 0) != 0 ||
        name.size() <= kArchivePrefix.size() + kArchiveExtension.size() ||
        name.substr(name.size() - kArchiveExtension.size()) !=
            kArchiveExtension) {
        return std::nullopt;
    }
    const auto digits = name.substr(
        kArchivePrefix.size(),
        name.size() - kArchivePrefix.size() - kArchiveExtension.size());
    const auto index = parseIndex(digits);
    return index ? std::optional{ArchiveIdentity{*index, false}}
                 : std::nullopt;
}

void validateManagedFile(
    const std::filesystem::path& path,
    std::size_t maxFileBytes) {
    const auto kind = classifyManagedPathNoFollow(path);
    if (kind == ManagedPathKind::Unsafe) {
        throw std::runtime_error("JSON Lines log path is not a regular file");
    }
    if (kind == ManagedPathKind::Regular &&
        std::filesystem::file_size(path) > maxFileBytes) {
        throw std::runtime_error("existing JSON Lines log file exceeds limit");
    }
}

void validateOptions(const JsonLinesLoggerOptions& options) {
    constexpr auto maxArchives =
        spdlog::sinks::rotating_file_sink_mt::MaxFiles;
    if (options.logDirectory.empty() || options.maxFileBytes == 0 ||
        options.maxFiles == 0 || options.maxFiles - 1 > maxArchives) {
        throw std::invalid_argument("invalid JSON Lines logger options");
    }
}

std::vector<std::filesystem::path> inspectArchives(
    const JsonLinesLoggerOptions& options) {
    std::vector<std::filesystem::path> expired;
    for (const auto& entry :
         std::filesystem::directory_iterator(options.logDirectory)) {
        const auto identity = archiveIdentity(
            entry.path().filename().string());
        if (!identity) continue;
        validateManagedFile(entry.path(), options.maxFileBytes);
        if (identity->legacy || identity->index >= options.maxFiles) {
            expired.push_back(entry.path());
        }
    }
    return expired;
}

void removeExpired(const std::vector<std::filesystem::path>& expired) {
    for (const auto& path : expired) {
        std::error_code error;
        const auto removed = std::filesystem::remove(path, error);
        if (!removed || error) {
            throw std::runtime_error("failed to remove expired log archive");
        }
    }
}

}  // namespace

std::filesystem::path prepareLogFiles(
    const JsonLinesLoggerOptions& options) {
    validateOptions(options);
    std::filesystem::create_directories(options.logDirectory);
    const auto active = options.logDirectory / kActiveFilename;
    validateManagedFile(active, options.maxFileBytes);
    // Complete inspection precedes cleanup so unsafe or oversized paths leave
    // the directory snapshot untouched when construction is rejected.
    const auto expired = inspectArchives(options);
    removeExpired(expired);
    return active;
}

}  // namespace vna::logging
