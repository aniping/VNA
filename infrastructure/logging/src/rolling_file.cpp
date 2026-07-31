#include "rolling_file.hpp"

#include "managed_path.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace vna::logging {

RollingFile::RollingFile(
    std::filesystem::path activePath,
    std::size_t maxFileBytes,
    std::size_t maxFiles)
    : activePath_(std::move(activePath)),
      maxFileBytes_(maxFileBytes),
      maxFiles_(maxFiles) {
    validateManagedFile(activePath_);
    removeExpiredArchives();
    openActive(std::ios::app);
    std::error_code error;
    const auto fileBytes = std::filesystem::file_size(activePath_, error);
    if (error) {
        throw std::runtime_error("failed to inspect JSON Lines log file");
    }
    currentBytes_ = fileBytes > maxFileBytes_
        ? maxFileBytes_
        : static_cast<std::size_t>(fileBytes);
}

bool RollingFile::validateManagedFile(
    const std::filesystem::path& path) const {
    const auto kind = classifyManagedPathNoFollow(path);
    if (kind == ManagedPathKind::Missing) {
        return false;
    }
    if (kind == ManagedPathKind::Unsafe) {
        throw std::runtime_error("JSON Lines log path is not a regular file");
    }
    if (std::filesystem::file_size(path) > maxFileBytes_) {
        throw std::runtime_error("existing JSON Lines log file exceeds limit");
    }
    return true;
}

void RollingFile::removeExpiredArchives() {
    const auto prefix = activePath_.filename().string() + ".";
    std::vector<std::filesystem::path> expired;
    for (const auto& entry :
         std::filesystem::directory_iterator(activePath_.parent_path())) {
        const auto name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string_view suffix{name.data() + prefix.size(),
                                      name.size() - prefix.size()};
        const bool numeric = !suffix.empty() &&
            std::all_of(suffix.begin(), suffix.end(), [](char value) {
                return value >= '0' && value <= '9';
            });
        if (!numeric) {
            continue;
        }
        if (classifyManagedPathNoFollow(entry.path()) !=
            ManagedPathKind::Regular) {
            throw std::runtime_error("JSON Lines archive is not a regular file");
        }
        std::size_t index = 0;
        const auto parsed = std::from_chars(
            suffix.data(), suffix.data() + suffix.size(), index);
        if (parsed.ec != std::errc{} || index == 0 || index >= maxFiles_ ||
            suffix != std::to_string(index)) {
            expired.push_back(entry.path());
            continue;
        }
        validateManagedFile(entry.path());
    }
    for (const auto& path : expired) {
        std::filesystem::remove(path);
    }
}

bool RollingFile::write(const std::string& line) noexcept {
    try {
        if (line.size() > maxFileBytes_) {
            return false;
        }
        if (currentBytes_ > maxFileBytes_ - line.size()) {
            rotate();
        }
        file_.write(line.data(), static_cast<std::streamsize>(line.size()));
        if (!file_.good()) {
            return false;
        }
        currentBytes_ += line.size();
        return true;
    } catch (...) {
        return false;
    }
}

bool RollingFile::flush() noexcept {
    try {
        file_.flush();
        return file_.good();
    } catch (...) {
        return false;
    }
}

void RollingFile::rotate() {
    file_.close();
    if (file_.fail()) {
        throw std::runtime_error("failed to close JSON Lines log file");
    }
    if (!validateManagedFile(activePath_)) {
        throw std::runtime_error("JSON Lines active file is missing");
    }
    if (maxFiles_ == 1) {
        openActive(std::ios::trunc);
        currentBytes_ = 0;
        return;
    }

    const auto oldest = archivePath(maxFiles_ - 1);
    if (validateManagedFile(oldest)) {
        if (!std::filesystem::remove(oldest)) {
            throw std::runtime_error("failed to remove JSON Lines archive");
        }
    }
    for (std::size_t index = maxFiles_ - 1; index > 1; --index) {
        const auto source = archivePath(index - 1);
        if (validateManagedFile(source)) {
            std::filesystem::rename(source, archivePath(index));
        }
    }
    if (!validateManagedFile(activePath_)) {
        throw std::runtime_error("JSON Lines active file is missing");
    }
    std::filesystem::rename(activePath_, archivePath(1));
    openActive(std::ios::trunc);
    currentBytes_ = 0;
}

void RollingFile::openActive(std::ios::openmode mode) {
    file_.clear();
    file_.open(activePath_, std::ios::binary | mode);
    if (!file_) {
        throw std::runtime_error("failed to open JSON Lines log file");
    }
}

std::filesystem::path RollingFile::archivePath(std::size_t index) const {
    auto path = activePath_;
    path += "." + std::to_string(index);
    return path;
}

}  // namespace vna::logging
