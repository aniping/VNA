#include <gtest/gtest.h>

#include <filesystem>
#include <system_error>

#include <vna/platform/executable_path.hpp>

namespace vna::platform {
namespace {

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& replacement)
        : original_(std::filesystem::current_path()) {
        std::filesystem::current_path(replacement);
    }

    ~ScopedCurrentPath() {
        std::error_code ignored;
        std::filesystem::current_path(original_, ignored);
    }

private:
    std::filesystem::path original_;
};

TEST(ExecutablePathContractTest, ReturnsExistingAbsoluteExecutablePath) {
    const auto executable = currentExecutablePath();

    EXPECT_TRUE(executable.is_absolute());
    EXPECT_TRUE(std::filesystem::exists(executable));
    EXPECT_TRUE(std::filesystem::is_regular_file(executable));
    EXPECT_EQ(executable.stem(), "vna_platform_contract_tests");
}

TEST(ExecutablePathContractTest, DoesNotDependOnWorkingDirectory) {
    const auto before = currentExecutablePath();
    const auto originalDirectory = std::filesystem::current_path();
    const auto temporary = std::filesystem::temp_directory_path();

    {
        // Release lookup must remain anchored to bin/vna-server even when a
        // launcher or user starts the process from an unrelated directory.
        const ScopedCurrentPath changedDirectory{temporary};
        EXPECT_EQ(currentExecutablePath(), before);
    }

    EXPECT_EQ(std::filesystem::current_path(), originalDirectory);
}

}  // namespace
}  // namespace vna::platform
