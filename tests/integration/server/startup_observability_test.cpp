#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include <vna/server/startup_observability.hpp>

namespace vna::server {
namespace {

class RecordingLogger final : public observability::Logger {
public:
    bool write(observability::LogEvent event) noexcept override {
        events.push_back(std::move(event));
        return true;
    }

    bool flush() noexcept override { return true; }

    std::vector<observability::LogEvent> events;
};

TEST(StartupObservabilityTest, WritesStableStartupMilestonesInOrder) {
    RecordingLogger logger;

    ASSERT_TRUE(writeStartupMilestones(logger, "instrument-1"));

    const std::vector<std::pair<std::string, std::string>> expected{
        {"server.lifecycle", "starting"},
        {"server.factory_preset", "loaded"},
        {"server.continuous_acquisition", "running"},
        {"server.display_publication", "running"},
        {"server.web_listener", "starting"},
    };
    ASSERT_EQ(logger.events.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(logger.events[index].level, observability::LogLevel::Info);
        EXPECT_EQ(logger.events[index].name, expected[index].first);
        EXPECT_EQ(logger.events[index].status, expected[index].second);
        EXPECT_EQ(logger.events[index].instrumentId, "instrument-1");
    }
}

TEST(StartupObservabilityTest, DistinguishesListenFailureFromStopped) {
    RecordingLogger logger;

    ASSERT_TRUE(writeListenFailed(logger, "instrument-1"));
    ASSERT_TRUE(writeStopped(logger, "instrument-1"));

    ASSERT_EQ(logger.events.size(), 2U);
    EXPECT_EQ(logger.events[0].level, observability::LogLevel::Error);
    EXPECT_EQ(logger.events[0].name, "server.web_listener");
    EXPECT_EQ(logger.events[0].status, "listen_failed");
    EXPECT_EQ(logger.events[1].level, observability::LogLevel::Info);
    EXPECT_EQ(logger.events[1].name, "server.lifecycle");
    EXPECT_EQ(logger.events[1].status, "stopped");
}

}  // namespace
}  // namespace vna::server
