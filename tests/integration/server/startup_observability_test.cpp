#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include <vna/application/factory_preset.hpp>
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
    const auto details = makeStartupLogDetails(
        application::makeFactoryPreset(),
        "instrument-1",
        "127.0.0.1",
        8080);

    ASSERT_TRUE(writeStartupMilestones(logger, details));

    struct Expected { const char* event; const char* status; const char* message; };
    const std::vector<Expected> expected{
        {"server.lifecycle", "starting", "Starting Vector Network Analyzer server"},
        {"server.factory_preset", "loaded", "Factory preset loaded: Channel 1, S21, Trace 1, 201 points, 10 MHz–26.5 GHz"},
        {"server.continuous_acquisition", "running", "Continuous acquisition started: 100 ms, ports 1/2, IFBW 10 kHz, power -10 dBm"},
        {"server.display_publication", "running", "Live display publication started: Trace 1, Log Magnitude"},
        {"server.web_listener", "starting", "Starting Web service at http://127.0.0.1:8080/"},
    };
    ASSERT_EQ(logger.events.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(logger.events[index].level, observability::LogLevel::Info);
        EXPECT_EQ(logger.events[index].name, expected[index].event);
        EXPECT_EQ(logger.events[index].status, expected[index].status);
        EXPECT_EQ(logger.events[index].message, expected[index].message);
        EXPECT_EQ(logger.events[index].instrumentId, "instrument-1");
    }
}

TEST(StartupObservabilityTest, DistinguishesListenFailureFromStopped) {
    RecordingLogger logger;
    const auto details = makeStartupLogDetails(
        application::makeFactoryPreset(),
        "instrument-1",
        "127.0.0.1",
        8080);

    ASSERT_TRUE(writeListenFailed(logger, details));
    ASSERT_TRUE(writeStopped(logger, details));

    ASSERT_EQ(logger.events.size(), 2U);
    EXPECT_EQ(logger.events[0].level, observability::LogLevel::Error);
    EXPECT_EQ(logger.events[0].name, "server.web_listener");
    EXPECT_EQ(logger.events[0].status, "listen_failed");
    EXPECT_EQ(logger.events[0].message,
              "Web service failed to listen at http://127.0.0.1:8080/");
    EXPECT_EQ(logger.events[1].level, observability::LogLevel::Info);
    EXPECT_EQ(logger.events[1].name, "server.lifecycle");
    EXPECT_EQ(logger.events[1].status, "stopped");
    EXPECT_EQ(logger.events[1].message, "Vector Network Analyzer server stopped");
}

}  // namespace
}  // namespace vna::server
