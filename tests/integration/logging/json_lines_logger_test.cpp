#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
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

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class ControlledStreamBuffer : public std::streambuf {
public:
    enum class Behavior { Block, Fail };

    explicit ControlledStreamBuffer(Behavior behavior) : behavior_(behavior) {}

    void waitForWrite() {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return writeStarted_; });
    }

    void release() {
        const std::scoped_lock lock{mutex_};
        released_ = true;
        changed_.notify_all();
    }

protected:
    std::streamsize xsputn(const char*, std::streamsize count) override {
        std::unique_lock lock{mutex_};
        writeStarted_ = true;
        changed_.notify_all();
        if (behavior_ == Behavior::Block) {
            changed_.wait(lock, [this] { return released_; });
            return count;
        }
        return 0;
    }

private:
    Behavior behavior_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool writeStarted_{false};
    bool released_{false};
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

TEST(JsonLinesLoggerTest, WritesStructuredEventToConsoleAndFile) {
    using namespace std::chrono_literals;
    TemporaryDirectory directory;
    std::ostringstream console;
    auto logger = makeJsonLinesLogger({directory.path(), &console});

    const std::array eventNames{"command.started", "command.completed",
                                "state.published"};
    for (const auto* name : eventNames) {
        ASSERT_EQ(logger->submit(completeEvent(name)),
                  observability::SubmitResult::Accepted);
    }
    ASSERT_TRUE(logger->flush(1s));

    const auto consoleLine = console.str();
    ASSERT_EQ(consoleLine.back(), '\n');
    ASSERT_EQ(consoleLine, readFile(directory.path() / "vna.log.jsonl"));
    std::istringstream lines{consoleLine};
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

TEST(JsonLinesLoggerTest, FlushTimeoutDoesNotForgetAcceptedEvent) {
    using namespace std::chrono_literals;
    TemporaryDirectory directory;
    ControlledStreamBuffer buffer{ControlledStreamBuffer::Behavior::Block};
    std::ostream console{&buffer};
    auto logger = makeJsonLinesLogger({directory.path(), &console});

    ASSERT_EQ(logger->submit(completeEvent("blocked.event")),
              observability::SubmitResult::Accepted);
    buffer.waitForWrite();
    EXPECT_FALSE(logger->flush(100ms));
    buffer.release();
    EXPECT_TRUE(logger->flush(1s));
}

TEST(JsonLinesLoggerTest, SinkFailureStopsFurtherSubmissions) {
    using namespace std::chrono_literals;
    TemporaryDirectory directory;
    ControlledStreamBuffer buffer{ControlledStreamBuffer::Behavior::Fail};
    std::ostream console{&buffer};
    auto logger = makeJsonLinesLogger({directory.path(), &console});

    ASSERT_EQ(logger->submit(completeEvent("failing.event")),
              observability::SubmitResult::Accepted);
    EXPECT_FALSE(logger->flush(1s));
    EXPECT_TRUE(readFile(directory.path() / "vna.log.jsonl").empty());
    EXPECT_EQ(logger->submit(completeEvent("after.failure")),
              observability::SubmitResult::Stopped);
    EXPECT_EQ(logger->statistics().sinkFailures, 1U);
}

}  // namespace
}  // namespace vna::logging
