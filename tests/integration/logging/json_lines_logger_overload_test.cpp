#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <utility>

#include <vna/logging/json_lines_logger.hpp>

namespace vna::logging {
namespace {
using namespace std::chrono_literals;

class JsonLinesLoggerOverloadTest : public ::testing::Test {
protected:
    JsonLinesLoggerOverloadTest()
        : directory_(std::filesystem::temp_directory_path() /
                ("vna-overload-test-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_);
    }

    ~JsonLinesLoggerOverloadTest() override {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::filesystem::path directory_;
    std::ostringstream console_;
};

class FirstWriteGateBuffer : public std::streambuf {
public:
    explicit FirstWriteGateBuffer(bool failAfterFirst = false)
        : failAfterFirst_(failAfterFirst) {}

    void waitForFirstWrite() {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return writeCount_ != 0; });
    }

    void release() {
        const std::scoped_lock lock{mutex_};
        released_ = true;
        changed_.notify_all();
    }

    void onFirstWrite(std::function<void()> action) {
        firstWriteAction_ = std::move(action);
    }

    [[nodiscard]] std::string contents() const {
        const std::scoped_lock lock{mutex_};
        return contents_;
    }

protected:
    std::streamsize xsputn(const char* data, std::streamsize count) override {
        std::unique_lock lock{mutex_};
        ++writeCount_;
        if (writeCount_ == 1) {
            changed_.notify_all();
            if (firstWriteAction_) {
                auto action = firstWriteAction_;
                lock.unlock();
                action();
                lock.lock();
            } else {
                changed_.wait(lock, [this] { return released_; });
            }
        }
        if (failAfterFirst_ && writeCount_ > 1) {
            return 0;
        }
        contents_.append(data, static_cast<std::size_t>(count));
        return count;
    }

private:
    bool failAfterFirst_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::string contents_;
    std::function<void()> firstWriteAction_;
    std::size_t writeCount_{0};
    bool released_{false};
};

observability::LogEvent event(observability::LogLevel level, std::string name) {
    return {.level = level, .name = std::move(name)};
}

TEST_F(JsonLinesLoggerOverloadTest, RejectsZeroCapacityWithoutFileSystemEffects) {
    const auto target = directory_ / "not-created";

    EXPECT_THROW(makeJsonLinesLogger({target, &console_, 0}), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(target));
}

TEST_F(JsonLinesLoggerOverloadTest, RejectsOversizedEncodedRecord) {
    auto logger = makeJsonLinesLogger({directory_, &console_, 2});

    EXPECT_EQ(logger->submit(event(
                  observability::LogLevel::Info, std::string(65 * 1024, 'x'))),
              observability::SubmitResult::RejectedOversized);
    EXPECT_TRUE(logger->flush(1s));
    EXPECT_EQ(logger->statistics().rejectedOversized, 1U);
    EXPECT_TRUE(console_.str().empty());
}

TEST_F(JsonLinesLoggerOverloadTest, DropsNewLowSeverityWhenQueueIsFull) {
    FirstWriteGateBuffer buffer;
    std::ostream console{&buffer};
    auto logger = makeJsonLinesLogger({directory_, &console, 1});

    ASSERT_EQ(logger->submit(event(observability::LogLevel::Info, "in-flight")),
              observability::SubmitResult::Accepted);
    buffer.waitForFirstWrite();
    ASSERT_EQ(logger->submit(event(observability::LogLevel::Info, "queued")),
              observability::SubmitResult::Accepted);
    EXPECT_EQ(logger->submit(event(observability::LogLevel::Debug, "dropped")),
              observability::SubmitResult::DroppedLowSeverity);
    buffer.release();
    EXPECT_TRUE(logger->flush(1s));
    EXPECT_EQ(logger->statistics().droppedLowSeverity, 1U);
    EXPECT_EQ(buffer.contents().find("dropped"), std::string::npos);
}

TEST_F(JsonLinesLoggerOverloadTest, EvictionIsReportedByOneFlushInterval) {
    FirstWriteGateBuffer buffer;
    std::ostream console{&buffer};
    auto logger = makeJsonLinesLogger({directory_, &console, 2});

    ASSERT_EQ(logger->submit(event(observability::LogLevel::Error, "in-flight")),
              observability::SubmitResult::Accepted);
    buffer.waitForFirstWrite();
    ASSERT_EQ(logger->submit(event(observability::LogLevel::Info, "oldest-low")),
              observability::SubmitResult::Accepted);
    ASSERT_EQ(logger->submit(event(observability::LogLevel::Debug, "kept-low")),
              observability::SubmitResult::Accepted);
    EXPECT_EQ(logger->submit(event(observability::LogLevel::Warning, "warning")),
              observability::SubmitResult::Accepted);
    buffer.release();

    EXPECT_FALSE(logger->flush(1s));
    EXPECT_TRUE(logger->flush(1s));
    EXPECT_EQ(logger->statistics().droppedLowSeverity, 1U);
    EXPECT_EQ(buffer.contents().find("oldest-low"), std::string::npos);
}

class HighQueueFallbackTest
    : public JsonLinesLoggerOverloadTest,
      public ::testing::WithParamInterface<bool> {};

TEST_P(HighQueueFallbackTest, UsesConsoleFallbackWhenHighQueueIsFull) {
    const bool failFallback = GetParam();
    FirstWriteGateBuffer buffer{failFallback};
    std::ostream console{&buffer};
    auto logger = makeJsonLinesLogger({directory_, &console, 1});

    std::promise<observability::SubmitResult> result;
    auto observed = result.get_future();
    buffer.onFirstWrite([&] {
        logger->submit(event(observability::LogLevel::Warning, "queued"));
        result.set_value(logger->submit(event(
            observability::LogLevel::Error,
            failFallback ? "rejected" : "fallback")));
    });

    ASSERT_EQ(logger->submit(event(observability::LogLevel::Error, "in-flight")),
              observability::SubmitResult::Accepted);
    const auto fallback = observed.get();
    if (failFallback) {
        EXPECT_EQ(fallback, observability::SubmitResult::RejectedHighSeverity);
        EXPECT_EQ(logger->statistics().rejectedHighSeverity, 1U);
        EXPECT_EQ(logger->statistics().sinkFailures, 1U);
        EXPECT_EQ(logger->submit(event(observability::LogLevel::Error, "stopped")),
                  observability::SubmitResult::Stopped);
        EXPECT_FALSE(logger->flush(1s));
    } else {
        EXPECT_EQ(fallback, observability::SubmitResult::EmergencyFallback);
        EXPECT_TRUE(logger->flush(1s));
        EXPECT_EQ(logger->statistics().emergencyFallbacks, 1U);
        EXPECT_NE(buffer.contents().find("fallback"), std::string::npos);
    }
}

INSTANTIATE_TEST_SUITE_P(SinkResult, HighQueueFallbackTest,
                         ::testing::Values(false, true));

}  // namespace
}  // namespace vna::logging
