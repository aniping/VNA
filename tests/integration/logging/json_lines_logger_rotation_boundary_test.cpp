#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <vna/logging/json_lines_logger.hpp>

namespace vna::logging {
namespace {
using namespace std::chrono_literals;

std::filesystem::path suffixed(
    const std::filesystem::path& path,
    std::string_view suffix) {
    auto result = path;
    result += suffix;
    return result;
}

void writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream{path, std::ios::binary} << text;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

std::size_t entryCount(const std::filesystem::path& directory) {
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& entry :
         std::filesystem::directory_iterator(directory)) {
        ++count;
    }
    return count;
}

observability::LogEvent event(std::string name) {
    observability::LogEvent result{};
    result.level = observability::LogLevel::Info;
    result.name = std::move(name);
    return result;
}

class JsonLinesLoggerRotationBoundaryTest : public ::testing::Test {
protected:
    JsonLinesLoggerRotationBoundaryTest()
        : directory_(std::filesystem::temp_directory_path() /
              ("vna-rotation-boundary-" + std::to_string(
                  std::chrono::steady_clock::now()
                      .time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_);
    }

    ~JsonLinesLoggerRotationBoundaryTest() override {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::filesystem::path directory_;
    std::ostringstream console_;
};

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       RejectsZeroRotationLimitsWithoutFileSystemEffects) {
    for (const bool zeroFileBytes : {false, true}) {
        const auto state = directory_ /
            (zeroFileBytes ? "zero-bytes" : "zero-files");
        JsonLinesLoggerOptions options{state, &console_};
        options.maxFileBytes = zeroFileBytes ? 0 : 128;
        options.maxFiles = zeroFileBytes ? 2 : 0;

        EXPECT_THROW({
            auto logger = makeJsonLinesLogger(options);
            (void)logger;
        }, std::invalid_argument);
        EXPECT_FALSE(std::filesystem::exists(state));
    }
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       RejectsOversizedManagedFilesWithoutChangingSnapshot) {
    for (const bool oversizedActive : {false, true}) {
        const auto state = directory_ /
            (oversizedActive ? "active-oversized" : "archive-oversized");
        std::filesystem::create_directory(state);
        const auto active = state / "vna.log.jsonl";
        const std::vector<std::pair<std::filesystem::path, std::string>> files{
            {active, oversizedActive ? std::string(9, 'a') : "active"},
            {suffixed(active, ".1"),
             oversizedActive ? "one" : std::string(9, 'b')},
            {suffixed(active, ".2"), "two"},
            {suffixed(active, ".9"), "nine"},
            {suffixed(active, ".01"), "leading"},
            {suffixed(active, ".backup"), "unrelated"},
        };
        for (const auto& [path, contents] : files) {
            writeText(path, contents);
        }
        JsonLinesLoggerOptions options{state, &console_};
        options.maxFileBytes = 8;
        options.maxFiles = 2;

        EXPECT_THROW({
            auto logger = makeJsonLinesLogger(options);
            (void)logger;
        }, std::runtime_error);
        EXPECT_EQ(entryCount(state), files.size());
        for (const auto& [path, contents] : files) {
            EXPECT_TRUE(std::filesystem::is_regular_file(path));
            EXPECT_EQ(readText(path), contents);
            EXPECT_EQ(std::filesystem::file_size(path), contents.size());
        }
    }
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       RetainsOnlyConfiguredActiveAndArchiveFiles) {
    for (const std::size_t maxFiles : {1U, 3U}) {
        const auto state = directory_ / ("retention-" +
            std::to_string(maxFiles));
        JsonLinesLoggerOptions options{state, &console_};
        options.maxFileBytes = 180;
        options.maxFiles = maxFiles;
        auto logger = makeJsonLinesLogger(options);
        for (const char value : {'a', 'b', 'c', 'd', 'e'}) {
            ASSERT_EQ(logger->submit(event(std::string(80, value))),
                      observability::SubmitResult::Accepted);
        }
        ASSERT_TRUE(logger->flush(1s));

        const auto active = state / "vna.log.jsonl";
        std::string retained;
        for (std::size_t index = 0; index < maxFiles; ++index) {
            const auto path = index == 0
                ? active : suffixed(active, "." + std::to_string(index));
            ASSERT_TRUE(std::filesystem::is_regular_file(path));
            EXPECT_LE(std::filesystem::file_size(path), options.maxFileBytes);
            retained += readText(path);
        }
        EXPECT_EQ(entryCount(state), maxFiles);
        for (const char value : {'a', 'b', 'c', 'd', 'e'}) {
            const bool expected = value == 'e' ||
                (maxFiles == 3 && value >= 'c');
            EXPECT_EQ(retained.find(std::string(80, value)) != std::string::npos,
                      expected);
        }
    }
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       ArchiveDirectoryConflictStopsLoggerWithoutRemovingDirectory) {
    const auto state = directory_ / "directory-conflict";
    JsonLinesLoggerOptions options{state, &console_};
    options.maxFileBytes = 180;
    options.maxFiles = 2;
    auto logger = makeJsonLinesLogger(options);
    const auto conflict = suffixed(state / "vna.log.jsonl", ".1");
    std::filesystem::create_directory(conflict);

    for (const char value : {'a', 'b'}) {
        ASSERT_EQ(logger->submit(event(std::string(80, value))),
                  observability::SubmitResult::Accepted);
    }
    EXPECT_FALSE(logger->flush(1s));
    EXPECT_EQ(logger->statistics().sinkFailures, 1U);
    EXPECT_EQ(logger->submit(event("after-failure")),
              observability::SubmitResult::Stopped);
    EXPECT_TRUE(std::filesystem::is_directory(conflict));
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       AcceptsExactEncodedLimitAndRejectsOneByteOver) {
    constexpr std::size_t limit = 512;
    std::ostringstream probeConsole;
    auto probe = makeJsonLinesLogger({directory_ / "probe", &probeConsole});
    ASSERT_EQ(probe->submit(event("")),
              observability::SubmitResult::Accepted);
    ASSERT_TRUE(probe->flush(1s));
    const auto baseBytes = probeConsole.str().size();
    ASSERT_LT(baseBytes, limit);

    JsonLinesLoggerOptions exactOptions{directory_ / "exact", &console_};
    exactOptions.maxFileBytes = limit;
    exactOptions.maxFiles = 2;
    auto exact = makeJsonLinesLogger(exactOptions);
    const std::string exactName(limit - baseBytes, 'x');
    ASSERT_EQ(exact->submit(event(exactName)),
              observability::SubmitResult::Accepted);
    ASSERT_TRUE(exact->flush(1s));
    EXPECT_EQ(std::filesystem::file_size(
                  exactOptions.logDirectory / "vna.log.jsonl"),
              limit);

    std::ostringstream rejectedConsole;
    JsonLinesLoggerOptions rejectedOptions{
        directory_ / "rejected", &rejectedConsole};
    rejectedOptions.maxFileBytes = limit;
    auto rejected = makeJsonLinesLogger(rejectedOptions);
    EXPECT_EQ(rejected->submit(event(exactName + "x")),
              observability::SubmitResult::RejectedOversized);
    EXPECT_EQ(rejected->statistics().rejectedOversized, 1U);
    EXPECT_TRUE(rejected->flush(1s));
    EXPECT_TRUE(rejectedConsole.str().empty());
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       DoesNotRotateWhenCumulativeBytesEqualFileLimit) {
    std::ostringstream probeConsole;
    auto probe = makeJsonLinesLogger({directory_ / "sum-probe", &probeConsole});
    for (const auto* name : {"first", "second"}) {
        ASSERT_EQ(probe->submit(event(name)),
                  observability::SubmitResult::Accepted);
    }
    ASSERT_TRUE(probe->flush(1s));
    const auto exactBytes = probeConsole.str().size();

    JsonLinesLoggerOptions options{directory_ / "sum-exact", &console_};
    options.maxFileBytes = exactBytes;
    options.maxFiles = 2;
    auto logger = makeJsonLinesLogger(options);
    for (const auto* name : {"first", "second"}) {
        ASSERT_EQ(logger->submit(event(name)),
                  observability::SubmitResult::Accepted);
    }
    ASSERT_TRUE(logger->flush(1s));

    const auto active = options.logDirectory / "vna.log.jsonl";
    EXPECT_EQ(std::filesystem::file_size(active), exactBytes);
    EXPECT_FALSE(std::filesystem::exists(suffixed(active, ".1")));
}

}  // namespace
}  // namespace vna::logging
