#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <streambuf>
#include <string>
#include <utility>

#include <vna/logging/json_lines_logger.hpp>

namespace vna::logging {
namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("vna-jsonl-test-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class FailingStreamBuffer : public std::streambuf {
protected:
    std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

observability::LogEvent completeEvent(std::string name) {
    return {
        .level = observability::LogLevel::Info,
        .name = std::move(name),
        .commandId = "command-1",
        .sessionId = "session-1",
        .instrumentId = "instrument-1",
        .stateRevision = 42,
        .status = "succeeded",
    };
}

TEST(JsonLinesLoggerTest, WritesStructuredEventsToConsoleAndFile) {
    TemporaryDirectory directory;
    std::ostringstream console;
    auto logger = makeJsonLinesLogger({directory.path(), &console});

    const std::array eventNames{"command.started", "command.completed",
                                "state.published"};
    for (const auto* name : eventNames) {
        ASSERT_TRUE(logger->write(completeEvent(name)));
    }
    ASSERT_TRUE(logger->flush());

    const auto consoleText = console.str();
    ASSERT_EQ(consoleText, readFile(directory.path() / "vna.log.jsonl"));
    std::istringstream lines{consoleText};
    const std::regex timestamp{R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$)"};
    for (const auto* name : eventNames) {
        std::string line;
        ASSERT_TRUE(std::getline(lines, line));
        const auto record = nlohmann::json::parse(line);
        EXPECT_EQ(record.at("level"), "info");
        EXPECT_EQ(record.at("event"), name);
        EXPECT_EQ(record.at("command_id"), "command-1");
        EXPECT_EQ(record.at("session_id"), "session-1");
        EXPECT_EQ(record.at("instrument_id"), "instrument-1");
        EXPECT_EQ(record.at("state_revision"), 42);
        EXPECT_EQ(record.at("status"), "succeeded");
        EXPECT_TRUE(std::regex_match(
            record.at("timestamp").get<std::string>(), timestamp));
    }
    EXPECT_EQ(lines.peek(), std::char_traits<char>::eof());
}

TEST(JsonLinesLoggerTest, WritesHumanConsoleAndStructuredFileFromSameEvent) {
    TemporaryDirectory directory;
    std::ostringstream console;
    auto options = JsonLinesLoggerOptions{directory.path(), &console};
    options.consoleFormat = ConsoleFormat::HumanReadable;
    auto logger = makeJsonLinesLogger(options);

    ASSERT_TRUE(logger->write(completeEvent("command.completed")));
    ASSERT_TRUE(logger->flush());

    const auto record = nlohmann::json::parse(
        readFile(directory.path() / "vna.log.jsonl"));
    const auto expected = record.at("timestamp").get<std::string>() +
        " [info] command.completed status=succeeded command_id=command-1" +
        " session_id=session-1 instrument_id=instrument-1" +
        " state_revision=42\n";
    EXPECT_EQ(console.str(), expected);
    EXPECT_THROW({
        const auto parsed = nlohmann::json::parse(console.str());
        static_cast<void>(parsed);
    }, nlohmann::json::parse_error);
}

TEST(JsonLinesLoggerTest, WritesOnlyToFileWhenConsoleIsDisabled) {
    TemporaryDirectory directory;
    auto options = JsonLinesLoggerOptions{.logDirectory = directory.path()};
    options.console = nullptr;
    auto logger = makeJsonLinesLogger(options);

    ASSERT_TRUE(logger->write(completeEvent("server.lifecycle")));
    ASSERT_TRUE(logger->flush());

    const auto record = nlohmann::json::parse(
        readFile(directory.path() / "vna.log.jsonl"));
    EXPECT_EQ(record.at("event"), "server.lifecycle");
}

TEST(JsonLinesLoggerTest, WritesThroughUnicodeLogDirectory) {
    TemporaryDirectory directory;
    const auto state = directory.path() /
        std::filesystem::path{u8"\u65e5\u5fd7"};
    auto options = JsonLinesLoggerOptions{.logDirectory = state};
    options.console = nullptr;
    auto logger = makeJsonLinesLogger(options);

    ASSERT_TRUE(logger->write(completeEvent("unicode.path")));
    ASSERT_TRUE(logger->flush());
    EXPECT_EQ(nlohmann::json::parse(readFile(state / "vna.log.jsonl"))
                  .at("event"),
              "unicode.path");
}

TEST(JsonLinesLoggerTest, SinkFailureIsReportedAndRemainsTerminal) {
    TemporaryDirectory directory;
    FailingStreamBuffer buffer;
    std::ostream console{&buffer};
    auto logger = makeJsonLinesLogger({directory.path(), &console});

    EXPECT_FALSE(logger->write(completeEvent("failing.event")));
    EXPECT_FALSE(logger->write(completeEvent("after.failure")));
    EXPECT_FALSE(logger->flush());
}

}  // namespace
}  // namespace vna::logging
