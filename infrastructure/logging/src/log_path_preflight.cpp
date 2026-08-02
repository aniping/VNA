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

constexpr std::string_view kHumanActive = "vna.log";
constexpr std::string_view kStructuredActive = "vna.jsonl";
constexpr std::string_view kArchivePrefix = "vna.";

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

std::optional<std::size_t> archiveIndex(std::string_view name) {
    if (name.rfind(kArchivePrefix, 0) != 0) return std::nullopt;
    for (const auto extension : {std::string_view{".log"},
                                 std::string_view{".jsonl"}}) {
        if (name.size() <= kArchivePrefix.size() + extension.size() ||
            name.substr(name.size() - extension.size()) != extension) {
            continue;
        }
        return parseIndex(name.substr(
            kArchivePrefix.size(),
            name.size() - kArchivePrefix.size() - extension.size()));
    }
    return std::nullopt;
}

void validateManagedFile(
    const std::filesystem::path& path,
    std::size_t maxFileBytes) {
    const auto kind = classifyManagedPathNoFollow(path);
    if (kind == ManagedPathKind::Unsafe) {
        throw std::runtime_error("managed log path is not a regular file");
    }
    if (kind == ManagedPathKind::Regular &&
        std::filesystem::file_size(path) > maxFileBytes) {
        throw std::runtime_error("existing managed log file exceeds limit");
    }
}

void validateOptions(const JsonLinesLoggerOptions& options) {
    constexpr auto maxArchives =
        spdlog::sinks::rotating_file_sink_mt::MaxFiles;
    if (options.logDirectory.empty() || options.maxFileBytes == 0 ||
        options.maxFiles == 0 || options.maxFiles - 1 > maxArchives) {
        throw std::invalid_argument("invalid logger options");
    }
}

std::vector<std::filesystem::path> inspectArchives(
    const JsonLinesLoggerOptions& options) {
    std::vector<std::filesystem::path> expired;
    for (const auto& entry :
         std::filesystem::directory_iterator(options.logDirectory)) {
        const auto index = archiveIndex(entry.path().filename().string());
        if (!index) continue;
        validateManagedFile(entry.path(), options.maxFileBytes);
        if (*index >= options.maxFiles) {
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

LogFilePaths prepareLogFiles(
    const JsonLinesLoggerOptions& options) {
    validateOptions(options);
    std::filesystem::create_directories(options.logDirectory);
    const LogFilePaths paths{
        options.logDirectory / kHumanActive,
        options.logDirectory / kStructuredActive,
    };
    validateManagedFile(paths.human, options.maxFileBytes);
    validateManagedFile(paths.structured, options.maxFileBytes);
    // Complete inspection precedes cleanup so unsafe or oversized paths leave
    // the directory snapshot untouched when construction is rejected.
    const auto expired = inspectArchives(options);
    removeExpired(expired);
    return paths;
}

}  // namespace vna::logging
