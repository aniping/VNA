#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <vna/logging/json_lines_logger.hpp>

namespace vna::logging {
namespace {
constexpr std::size_t kThreadCount = 8;
constexpr std::size_t kEventsPerThread = 20;
constexpr std::size_t kEventCount = kThreadCount * kEventsPerThread;
constexpr std::string_view kEventName = "logging.concurrent_submit";

class StartGate {
public:
    explicit StartGate(std::size_t participants)
        : participants_(participants) {}

    void arriveAndWait() {
        std::unique_lock lock{mutex_};
        ++arrived_;
        changed_.notify_all();
        changed_.wait(lock, [this] { return released_; });
    }

    void releaseWhenAllReady() {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return arrived_ == participants_; });
        released_ = true;
        lock.unlock();
        changed_.notify_all();
    }

private:
    const std::size_t participants_;
    std::size_t arrived_{0};
    bool released_{false};
    std::mutex mutex_;
    std::condition_variable changed_;
};

std::string eventId(std::size_t threadIndex, std::size_t eventIndex) {
    return "thread-" + std::to_string(threadIndex) + "-event-" +
        std::to_string(eventIndex);
}

observability::LogEvent concurrentEvent(
    std::string id,
    std::uint64_t revision) {
    observability::LogEvent event{};
    event.level = observability::LogLevel::Info;
    event.name = kEventName;
    event.commandId = std::move(id);
    event.sessionId = "concurrency-session";
    event.instrumentId = "instrument-1";
    event.stateRevision = revision;
    event.status = "submitted";
    return event;
}

std::vector<std::uint8_t> writeConcurrently(
    observability::Logger& logger) {
    StartGate gate{kThreadCount};
    std::vector<std::uint8_t> results(kEventCount, 0);
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    for (std::size_t threadIndex = 0;
         threadIndex < kThreadCount; ++threadIndex) {
        workers.emplace_back([&, threadIndex] {
            gate.arriveAndWait();
            for (std::size_t eventIndex = 0;
                 eventIndex < kEventsPerThread; ++eventIndex) {
                const auto index = threadIndex * kEventsPerThread + eventIndex;
                results[index] = static_cast<std::uint8_t>(logger.write(
                    concurrentEvent(eventId(threadIndex, eventIndex), index)));
            }
        });
    }
    gate.releaseWhenAllReady();
    for (auto& worker : workers) worker.join();
    return results;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

bool isManagedLogName(std::string_view name) {
    constexpr std::string_view active = "vna.log.jsonl";
    if (name == active) return true;
    constexpr std::string_view prefix = "vna.log.";
    constexpr std::string_view extension = ".jsonl";
    if (name.rfind(prefix, 0) != 0) return false;
    if (name.size() <= prefix.size() + extension.size() ||
        name.substr(name.size() - extension.size()) != extension) {
        return false;
    }
    const auto index = name.substr(
        prefix.size(), name.size() - prefix.size() - extension.size());
    return !index.empty() && index.front() != '0' &&
        std::all_of(index.begin(), index.end(), [](char value) {
            return value >= '0' && value <= '9';
        });
}

bool hasString(const nlohmann::json& record, const char* key) {
    const auto value = record.find(key);
    return value != record.end() && value->is_string();
}

struct ParsedLog {
    bool valid{true};
    bool unique{true};
    std::size_t lines{0};
    std::set<std::string> ids;
};

void appendJsonLines(std::string contents, ParsedLog& parsed) {
    if (contents.empty() || contents.back() != '\n') parsed.valid = false;
    std::istringstream input{std::move(contents)};
    std::string line;
    while (std::getline(input, line)) {
        ++parsed.lines;
        const auto record = nlohmann::json::parse(line, nullptr, false);
        const bool fieldsValid = !record.is_discarded() && record.is_object() &&
            hasString(record, "timestamp") && hasString(record, "level") &&
            hasString(record, "event") && hasString(record, "command_id") &&
            hasString(record, "session_id") &&
            hasString(record, "instrument_id") &&
            record.contains("state_revision") &&
            record["state_revision"].is_number_unsigned() &&
            hasString(record, "status");
        if (!fieldsValid) {
            parsed.valid = false;
            continue;
        }
        if (record["event"] != kEventName) parsed.valid = false;
        const auto id = record["command_id"].get<std::string>();
        if (!parsed.ids.insert(id).second) parsed.unique = false;
    }
}

struct FileOutput {
    ParsedLog log;
    std::size_t files{0};
    bool managedOnly{true};
    bool withinLimit{true};
};

FileOutput readFileOutput(
    const std::filesystem::path& directory,
    std::size_t maxFileBytes) {
    FileOutput output;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        ++output.files;
        output.managedOnly = output.managedOnly &&
            isManagedLogName(entry.path().filename().string());
        output.withinLimit = output.withinLimit &&
            entry.file_size() <= maxFileBytes;
        appendJsonLines(readText(entry.path()), output.log);
    }
    return output;
}

std::set<std::string> expectedIds() {
    std::set<std::string> expected;
    for (std::size_t threadIndex = 0;
         threadIndex < kThreadCount; ++threadIndex) {
        for (std::size_t eventIndex = 0;
             eventIndex < kEventsPerThread; ++eventIndex) {
            expected.insert(eventId(threadIndex, eventIndex));
        }
    }
    return expected;
}

class JsonLinesLoggerConcurrencyTest : public ::testing::Test {
protected:
    JsonLinesLoggerConcurrencyTest()
        : directory_(std::filesystem::temp_directory_path() /
              ("vna-concurrency-test-" + std::to_string(
                  std::chrono::steady_clock::now()
                      .time_since_epoch().count()))) {}

    ~JsonLinesLoggerConcurrencyTest() override {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::filesystem::path directory_;
    std::ostringstream console_;
};

TEST_F(JsonLinesLoggerConcurrencyTest,
       PreservesEveryJsonLineAcrossConcurrentSubmittersAndRotation) {
    JsonLinesLoggerOptions options{directory_, &console_};
    options.maxFileBytes = 1024;
    options.maxFiles = kEventCount;
    auto logger = makeJsonLinesLogger(options);

    const auto results = writeConcurrently(*logger);
    EXPECT_TRUE(std::all_of(results.begin(), results.end(), [](auto result) {
        return result == 1;
    }));
    ASSERT_TRUE(logger->flush());

    const auto files = readFileOutput(directory_, options.maxFileBytes);
    EXPECT_GT(files.files, 1U);
    EXPECT_LE(files.files, options.maxFiles);
    EXPECT_TRUE(files.managedOnly);
    EXPECT_TRUE(files.withinLimit);
    EXPECT_TRUE(files.log.valid);
    EXPECT_TRUE(files.log.unique);
    EXPECT_EQ(files.log.lines, kEventCount);
    EXPECT_EQ(files.log.ids, expectedIds());

    ParsedLog console;
    appendJsonLines(console_.str(), console);
    EXPECT_TRUE(console.valid);
    EXPECT_TRUE(console.unique);
    EXPECT_EQ(console.lines, kEventCount);
    EXPECT_EQ(console.ids, expectedIds());
}

}  // namespace
}  // namespace vna::logging
