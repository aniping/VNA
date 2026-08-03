#include <gtest/gtest.h>

#include <vna/compat/filesystem.hpp>
#include <system_error>

#include <vna/platform/executable_path.hpp>

namespace vna::platform {
namespace {

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(
        const vna::compat::filesystem::path& replacement)
        : original_(vna::compat::filesystem::current_path()) {
        vna::compat::filesystem::current_path(replacement);
    }

    ~ScopedCurrentPath() {
        std::error_code ignored;
        vna::compat::filesystem::current_path(original_, ignored);
    }

private:
    vna::compat::filesystem::path original_;
};

TEST(ExecutablePathContractTest, ReturnsExistingAbsoluteExecutablePath) {
    const auto executable = currentExecutablePath();

    EXPECT_TRUE(executable.is_absolute());
    EXPECT_TRUE(vna::compat::filesystem::exists(executable));
    EXPECT_TRUE(vna::compat::filesystem::is_regular_file(executable));
    EXPECT_EQ(executable.stem(), "vna_platform_contract_tests");
}

TEST(ExecutablePathContractTest, DoesNotDependOnWorkingDirectory) {
    const auto before = currentExecutablePath();
    const auto originalDirectory = vna::compat::filesystem::current_path();
    const auto temporary = vna::compat::filesystem::temp_directory_path();
    const auto changedDirectory = temporary != originalDirectory
        ? temporary
        : before.parent_path();
    ASSERT_NE(changedDirectory, originalDirectory);

    {
        // Release lookup must remain anchored to bin/vna-server even when a
        // launcher or user starts the process from an unrelated directory.
        const ScopedCurrentPath changedPath{changedDirectory};
        EXPECT_EQ(currentExecutablePath(), before);
    }

    EXPECT_EQ(vna::compat::filesystem::current_path(), originalDirectory);
}

}  // namespace
}  // namespace vna::platform
