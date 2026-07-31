#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace vna::logging {

class RollingFile {
public:
    RollingFile(
        std::filesystem::path activePath,
        std::size_t maxFileBytes,
        std::size_t maxFiles);

    bool write(const std::string& line) noexcept;
    bool flush() noexcept;

private:
    bool validateManagedFile(const std::filesystem::path& path) const;
    void removeExpiredArchives();
    void rotate();
    void openActive(std::ios::openmode mode);
    [[nodiscard]] std::filesystem::path archivePath(
        std::size_t index) const;

    std::filesystem::path activePath_;
    std::ofstream file_;
    std::size_t maxFileBytes_;
    std::size_t maxFiles_;
    std::size_t currentBytes_{0};
};

}  // namespace vna::logging
