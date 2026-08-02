#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <vna/observability/logger.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

class RecordingLogger final : public observability::Logger {
public:
    bool write(observability::LogEvent event) noexcept override {
        events.push_back(std::move(event));
        return writeSucceeds;
    }
    bool flush() noexcept override {
        ++flushCount;
        return flushSucceeds;
    }

    std::vector<observability::LogEvent> events;
    int flushCount{};
    bool writeSucceeds{true};
    bool flushSucceeds{true};
};

nlohmann::json createChannelRequest(
    std::string commandId,
    std::uint64_t expectedRevision) {
    return {
        {"commandId", std::move(commandId)},
        {"sessionId", "web-session"},
        {"instrumentId", "instrument-1"},
        {"expectedStateRevision", expectedRevision},
        {"type", "createChannel"},
        {"payload",
         {
             {"startFrequencyHz", 10'000'000},
             {"stopFrequencyHz", 26'500'000'000},
             {"points", 201},
             {"ifBandwidthHz", 10'000},
             {"powerDbm", -10.0},
         }},
    };
}

class WebApiCommandObservabilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        port_ = webApi_.bindToAnyPort("127.0.0.1");
        ASSERT_GT(port_, 0);
        serverThread_ = std::thread([this] {
            static_cast<void>(webApi_.listenAfterBind());
        });
        webApi_.waitUntilReady();
    }

    void TearDown() override {
        webApi_.stop();
        if (serverThread_.joinable()) serverThread_.join();
    }

    httplib::Result post(const nlohmann::json& request) const {
        return httplib::Client{"127.0.0.1", port_}.Post(
            "/api/v1/commands", request.dump(), "application/json");
    }

    application::OperationManager operations_;
    vna::test::StoppedCommandBus commandBus_{
        application::InstrumentId{"instrument-1"}};
    application::TraceDisplayFrameRepository repository_{1};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    RecordingLogger logger_;
    int failureReports_{};
    WebApi webApi_{
        commandBus_,
        operations_,
        query_,
        repository_,
        WebApiOptions{
            .logger = &logger_,
            .logFailureReporter = [this](std::string_view) {
                ++failureReports_;
            }}};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiCommandObservabilityTest, LogsAcceptedAndRejectedWebCommands) {
    ASSERT_EQ(post(createChannelRequest("create-channel", 0))->status, 200);
    ASSERT_EQ(post(createChannelRequest("stale-command", 0))->status, 409);

    ASSERT_EQ(logger_.events.size(), 2U);
    const auto& accepted = logger_.events[0];
    EXPECT_EQ(accepted.level, observability::LogLevel::Info);
    EXPECT_EQ(accepted.name, "web.command.create_channel");
    EXPECT_EQ(accepted.message, "Create channel succeeded");
    EXPECT_FALSE(accepted.errorCode.has_value());
    EXPECT_EQ(accepted.commandId, "create-channel");
    EXPECT_EQ(accepted.sessionId, "web-session");
    EXPECT_EQ(accepted.instrumentId, "instrument-1");
    EXPECT_EQ(accepted.stateRevision, 1U);
    EXPECT_EQ(accepted.status, "succeeded");

    const auto& rejected = logger_.events[1];
    EXPECT_EQ(rejected.level, observability::LogLevel::Warning);
    EXPECT_EQ(rejected.name, "web.command.create_channel");
    EXPECT_EQ(rejected.message, "Create channel rejected");
    EXPECT_EQ(rejected.errorCode, "state-revision-conflict");
    EXPECT_EQ(rejected.commandId, "stale-command");
    EXPECT_EQ(rejected.stateRevision, 1U);
    EXPECT_EQ(rejected.status, "rejected");
    EXPECT_EQ(logger_.flushCount, 2);
}

TEST_F(WebApiCommandObservabilityTest, DoesNotLogInvalidBodiesOrReads) {
    httplib::Client client{"127.0.0.1", port_};
    ASSERT_EQ(client.Get("/api/v1/state")->status, 200);
    ASSERT_EQ(client.Post(
                  "/api/v1/commands", "not-json", "application/json")
                  ->status,
              400);
    EXPECT_TRUE(logger_.events.empty());
}

TEST_F(WebApiCommandObservabilityTest, ReportsSinkFailureWithoutChangingHttp) {
    logger_.writeSucceeds = false;
    ASSERT_EQ(post(createChannelRequest("write-failure", 0))->status, 200);
    logger_.writeSucceeds = true;
    logger_.flushSucceeds = false;
    ASSERT_EQ(post(createChannelRequest("flush-failure", 1))->status, 200);

    EXPECT_EQ(commandBus_.snapshot().stateRevision, 2U);
    EXPECT_EQ(logger_.flushCount, 2);
    EXPECT_EQ(failureReports_, 2);
}

TEST_F(WebApiCommandObservabilityTest, RequiresReporterWhenLoggingIsEnabled) {
    EXPECT_THROW(
        WebApi(
            commandBus_,
            operations_,
            query_,
            repository_,
            WebApiOptions{.logger = &logger_}),
        std::invalid_argument);
}

}  // namespace
}  // namespace vna::web_api
