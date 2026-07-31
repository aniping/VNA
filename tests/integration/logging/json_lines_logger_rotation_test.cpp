#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

#include <vna/logging/json_lines_logger.hpp>

namespace vna::logging {
namespace {
using namespace std::chrono_literals;

std::error_code createTestSymlink(
    const std::filesystem::path& target,
    const std::filesystem::path& link) {
#ifdef _WIN32
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), 0)) return {};
    return {static_cast<int>(GetLastError()), std::system_category()};
#else
    std::error_code error;
    std::filesystem::create_symlink(target, link, error);
    return error;
#endif
}

bool isWindowsPrivilegeError(const std::error_code& error) {
#ifdef _WIN32
    return error.value() == ERROR_PRIVILEGE_NOT_HELD ||
        error.value() == ERROR_ACCESS_DENIED;
#else
    return false;
#endif
}

bool isTestSymlink(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return std::filesystem::symlink_status(path).type() ==
        std::filesystem::file_type::symlink;
#endif
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
}

class JsonLinesLoggerRotationTest : public ::testing::Test {
protected:
    JsonLinesLoggerRotationTest()
        : directory_(std::filesystem::temp_directory_path() /
                     ("vna-rotation-test-" + std::to_string(
                         std::chrono::steady_clock::now()
                             .time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_);
    }

    ~JsonLinesLoggerRotationTest() override {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::filesystem::path directory_;
    std::ostringstream console_;
};

TEST_F(JsonLinesLoggerRotationTest, RejectsManagedSymlinks) {
    for (const auto* suffix : {"", ".1"}) {
        const auto state = directory_ /
            (suffix[0] == '\0' ? "active-link" : "archive-link");
        std::filesystem::create_directory(state);
        const auto target = directory_ / (state.filename().string() + "-target");
        std::ofstream{target} << "outside";
        auto link = state / "vna.log.jsonl";
        link += suffix;
        const auto error = createTestSymlink(target, link);
        if (error && isWindowsPrivilegeError(error)) {
            GTEST_SKIP() << "symlink unavailable: " << error.message();
        }
        ASSERT_FALSE(error) << error.message();
        JsonLinesLoggerOptions options{state, &console_};

        EXPECT_THROW(makeJsonLinesLogger(options), std::runtime_error);
        EXPECT_TRUE(isTestSymlink(link));
        EXPECT_TRUE(std::filesystem::equivalent(link, target));
        EXPECT_EQ(readTextFile(target), "outside");
    }
}

TEST_F(JsonLinesLoggerRotationTest, ArchiveSymlinkConflictStopsLogger) {
    const auto state = directory_ / "state";
    JsonLinesLoggerOptions options{state, &console_};
    options.maxFileBytes = 180;
    options.maxFiles = 2;
    auto logger = makeJsonLinesLogger(options);
    const auto target = directory_ / "outside-target";
    std::ofstream{target} << "outside";
    auto conflict = state / "vna.log.jsonl";
    conflict += ".1";
    const auto error = createTestSymlink(target, conflict);
    if (error && isWindowsPrivilegeError(error)) {
        GTEST_SKIP() << "symlink unavailable: " << error.message();
    }
    ASSERT_FALSE(error) << error.message();

    for (const char value : {'a', 'b'}) {
        EXPECT_EQ(logger->submit({
                      .level = observability::LogLevel::Info,
                      .name = std::string(80, value),
                  }),
                  observability::SubmitResult::Accepted);
    }
    EXPECT_FALSE(logger->flush(1s));
    EXPECT_EQ(logger->statistics().sinkFailures, 1U);
    EXPECT_EQ(logger->submit({.level = observability::LogLevel::Warning,
                              .name = "after_failure"}),
              observability::SubmitResult::Stopped);
    EXPECT_TRUE(isTestSymlink(conflict));
    EXPECT_TRUE(std::filesystem::equivalent(conflict, target));
    EXPECT_EQ(readTextFile(target), "outside");
}

TEST_F(JsonLinesLoggerRotationTest, RotatesBeforeWritingFullFile) {
    JsonLinesLoggerOptions options{directory_, &console_};
    options.maxFileBytes = 180;
    options.maxFiles = 2;
    auto logger = makeJsonLinesLogger(options);
    for (const char value : {'a', 'b'}) {
        ASSERT_EQ(logger->submit({.level = observability::LogLevel::Info,
                                  .name = std::string(80, value)}),
                  observability::SubmitResult::Accepted);
    }
    ASSERT_TRUE(logger->flush(1s));

    const auto active = directory_ / "vna.log.jsonl";
    auto archive = active;
    archive += ".1";
    EXPECT_LE(std::filesystem::file_size(active), options.maxFileBytes);
    EXPECT_LE(std::filesystem::file_size(archive), options.maxFileBytes);
}

}  // namespace
}  // namespace vna::logging
