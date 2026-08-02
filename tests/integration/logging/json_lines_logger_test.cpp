#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
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

std::chrono::milliseconds parseTimestamp(std::string_view value) {
    std::tm fields{};
    std::istringstream input{std::string{value.substr(0, 19)}};
    input >> std::get_time(&fields, "%Y-%m-%dT%H:%M:%S");
#ifdef _WIN32
    const auto seconds = _mkgmtime64(&fields);
#else
    const auto seconds = timegm(&fields);
#endif
    auto offset = 0;
    if (value.size() > 24) {
        offset = std::stoi(std::string{value.substr(24, 2)}) * 3600 +
            std::stoi(std::string{value.substr(27, 2)}) * 60;
        if (value[23] == '-') offset = -offset;
    }
    return std::chrono::seconds{seconds - offset} +
        std::chrono::milliseconds{std::stoi(std::string{value.substr(20, 3)})};
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
    ASSERT_EQ(consoleText, readFile(directory.path() / "vna.jsonl"));
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

    const auto human = readFile(directory.path() / "vna.log");
    const auto record = nlohmann::json::parse(
        readFile(directory.path() / "vna.jsonl"));
    EXPECT_EQ(console.str(), human);
    const std::regex humanLine{
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}[+-]\d{2}:\d{2} \[info\] command\.completed status=succeeded command_id=command-1 session_id=session-1 instrument_id=instrument-1 state_revision=42\n$)"};
    EXPECT_TRUE(std::regex_match(human, humanLine));
    EXPECT_EQ(record.at("event"), "command.completed");
    EXPECT_EQ(parseTimestamp(std::string_view{human}.substr(0, 29)),
              parseTimestamp(record.at("timestamp").get<std::string>()));
    EXPECT_THROW({
        const auto parsed = nlohmann::json::parse(human);
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
        readFile(directory.path() / "vna.jsonl"));
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
    EXPECT_EQ(nlohmann::json::parse(readFile(state / "vna.jsonl"))
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
