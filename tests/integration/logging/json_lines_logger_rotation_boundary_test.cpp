#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <vna/logging/json_lines_logger.hpp>

namespace vna::logging {
namespace {

class JsonLinesLoggerRotationBoundaryTest : public ::testing::Test {
protected:
    JsonLinesLoggerRotationBoundaryTest()
        : root_(std::filesystem::temp_directory_path() /
                ("vna-rotation-boundary-" + std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch().count()))) {
        std::filesystem::create_directories(root_);
    }

    ~JsonLinesLoggerRotationBoundaryTest() override {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    std::filesystem::path root_;
};

observability::LogEvent event(std::string name) {
    return {
        .level = observability::LogLevel::Info,
        .name = std::move(name),
        .message = std::string(120, 'm'),
    };
}

JsonLinesLoggerOptions optionsFor(
    const std::filesystem::path& directory,
    std::size_t maxFileBytes,
    std::size_t maxFiles) {
    auto options = JsonLinesLoggerOptions{.logDirectory = directory};
    options.console = nullptr;
    options.maxFileBytes = maxFileBytes;
    options.maxFiles = maxFiles;
    return options;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

std::size_t fileCount(const std::filesystem::path& directory) {
    return static_cast<std::size_t>(std::distance(
        std::filesystem::directory_iterator{directory},
        std::filesystem::directory_iterator{}));
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       InvalidLimitsDoNotCreateLogDirectory) {
    for (const auto limits : {std::pair{0U, 1U}, std::pair{512U, 0U}}) {
        const auto state = root_ /
            ("invalid-" + std::to_string(limits.first));
        EXPECT_THROW(makeJsonLinesLogger(
            optionsFor(state, limits.first, limits.second)),
            std::invalid_argument);
        EXPECT_FALSE(std::filesystem::exists(state));
    }
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       OversizedManagedFileIsRejectedWithoutMutation) {
    for (const auto* filename :
         {"vna.log", "vna.1.log", "vna.jsonl", "vna.1.jsonl"}) {
        const auto state = root_ / filename;
        std::filesystem::create_directories(state);
        const auto managed = state / filename;
        const auto unrelated = state / "vna.log.backup";
        std::ofstream{managed, std::ios::binary} << std::string(513, 'm');
        std::ofstream{unrelated, std::ios::binary} << "keep";

        EXPECT_THROW(makeJsonLinesLogger(optionsFor(state, 512, 3)),
                     std::runtime_error);
        EXPECT_EQ(readText(managed), std::string(513, 'm'));
        EXPECT_EQ(readText(unrelated), "keep");
    }
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       RetainsConfiguredTotalFileCount) {
    for (const auto maxFiles : {1U, 3U}) {
        const auto state = root_ / ("retain-" + std::to_string(maxFiles));
        auto logger = makeJsonLinesLogger(optionsFor(state, 300, maxFiles));
        for (const char value : {'a', 'b', 'c', 'd', 'e'}) {
            ASSERT_TRUE(logger->write(event(std::string(80, value))));
        }
        ASSERT_TRUE(logger->flush());
        EXPECT_EQ(fileCount(state), maxFiles * 2U);
    }
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       LowerRetentionRemovesExpiredArchivesOnRestart) {
    const auto state = root_ / "retention-restart";
    auto logger = makeJsonLinesLogger(optionsFor(state, 300, 4));
    for (const char value : {'a', 'b', 'c', 'd', 'e'}) {
        ASSERT_TRUE(logger->write(event(std::string(80, value))));
    }
    ASSERT_TRUE(logger->flush());
    logger.reset();
    ASSERT_EQ(fileCount(state), 8U);

    logger = makeJsonLinesLogger(optionsFor(state, 300, 2));
    ASSERT_TRUE(logger->flush());
    EXPECT_EQ(fileCount(state), 4U);
    for (const auto* filename :
         {"vna.2.log", "vna.3.log", "vna.2.jsonl", "vna.3.jsonl"}) {
        EXPECT_FALSE(std::filesystem::exists(state / filename));
    }
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       AcceptsExactEncodedLimitAndRejectsOneByteOver) {
    const auto probeState = root_ / "probe";
    auto probe = makeJsonLinesLogger(optionsFor(probeState, 1024, 1));
    ASSERT_TRUE(probe->write(event("")));
    ASSERT_TRUE(probe->flush());
    const auto overhead = std::filesystem::file_size(
        probeState / "vna.jsonl");

    constexpr std::size_t payloadBytes = 32;
    const auto limit = overhead + payloadBytes;
    auto exact = makeJsonLinesLogger(optionsFor(root_ / "exact", limit, 1));
    ASSERT_TRUE(exact->write(event(std::string(payloadBytes, 'x'))));
    ASSERT_TRUE(exact->flush());
    EXPECT_EQ(std::filesystem::file_size(root_ / "exact" / "vna.jsonl"),
              limit);

    auto oversized = makeJsonLinesLogger(
        optionsFor(root_ / "oversized", limit, 1));
    EXPECT_FALSE(oversized->write(event(std::string(payloadBytes + 1, 'x'))));
    EXPECT_TRUE(oversized->flush());
    EXPECT_EQ(std::filesystem::file_size(
                  root_ / "oversized" / "vna.jsonl"), 0U);
}

TEST_F(JsonLinesLoggerRotationBoundaryTest,
       CumulativeExactLimitDoesNotRotateEarly) {
    const auto probeState = root_ / "cumulative-probe";
    auto probe = makeJsonLinesLogger(optionsFor(probeState, 4096, 2));
    for (const auto* name : {"first", "second"}) {
        ASSERT_TRUE(probe->write(event(name)));
    }
    ASSERT_TRUE(probe->flush());
    const auto exactLimit = std::filesystem::file_size(
        probeState / "vna.jsonl");

    const auto exactState = root_ / "cumulative-exact";
    auto logger = makeJsonLinesLogger(optionsFor(exactState, exactLimit, 2));
    for (const auto* name : {"first", "second"}) {
        ASSERT_TRUE(logger->write(event(name)));
    }
    ASSERT_TRUE(logger->flush());
    EXPECT_EQ(fileCount(exactState), 2U);
    EXPECT_EQ(std::filesystem::file_size(exactState / "vna.jsonl"),
              exactLimit);
}

}  // namespace
}  // namespace vna::logging
