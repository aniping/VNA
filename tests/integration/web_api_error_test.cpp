#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <thread>
#include <utility>

#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

nlohmann::json commandRequest(
    std::string commandId,
    std::uint64_t revision,
    std::string type,
    nlohmann::json payload,
    std::string instrumentId = "instrument-1") {
    return {
        {"commandId", std::move(commandId)},
        {"sessionId", "session-1"},
        {"instrumentId", std::move(instrumentId)},
        {"expectedStateRevision", revision},
        {"type", std::move(type)},
        {"payload", std::move(payload)},
    };
}

nlohmann::json sweepPayload() {
    return {
        {"startFrequencyHz", 10'000'000},
        {"stopFrequencyHz", 26'500'000'000},
        {"points", 201},
        {"ifBandwidthHz", 10'000},
        {"powerDbm", -10.0},
    };
}

class WebApiErrorTest : public ::testing::Test {
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
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    httplib::Result postCommand(const nlohmann::json& request) const {
        httplib::Client client{"127.0.0.1", port_};
        return client.Post(
            "/api/v1/commands", request.dump(), "application/json");
    }

    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"}};
    application::TraceDisplayFrameRepository repository_{1};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{commandBus_, query_};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiErrorTest, SuccessfulCommandOmitsErrorCode) {
    const auto response = postCommand(commandRequest(
        "create-channel", 0, "createChannel", sweepPayload()));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "succeeded");
    EXPECT_FALSE(body.contains("errorCode"));
}

TEST_F(WebApiErrorTest, MapsInvalidSweepToStableErrorCode) {
    auto payload = sweepPayload();
    payload["startFrequencyHz"] = 30'000'000'000;

    const auto response = postCommand(commandRequest(
        "invalid-sweep", 0, "createChannel", std::move(payload)));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::UnprocessableContent_422);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "validationError");
    EXPECT_EQ(body.at("errorCode"), "invalid-sweep-settings");
}

TEST_F(WebApiErrorTest, MapsWrongInstrumentToNotFound) {
    const auto response = postCommand(commandRequest(
        "wrong-instrument",
        0,
        "createChannel",
        sweepPayload(),
        "instrument-2"));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::NotFound_404);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "wrongInstrument");
    EXPECT_EQ(body.at("errorCode"), "wrong-instrument");
}

TEST_F(WebApiErrorTest, MapsRevisionConflictToStableErrorCode) {
    const auto created = postCommand(commandRequest(
        "create-channel", 0, "createChannel", sweepPayload()));
    ASSERT_TRUE(created);
    ASSERT_EQ(created->status, httplib::StatusCode::OK_200);

    const auto response = postCommand(commandRequest(
        "stale-command", 0, "createChannel", sweepPayload()));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::Conflict_409);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "conflict");
    EXPECT_EQ(body.at("errorCode"), "state-revision-conflict");
}

TEST_F(WebApiErrorTest, MapsMissingChannelToStableErrorCode) {
    auto payload = sweepPayload();
    payload["channelId"] = 99;

    const auto response = postCommand(commandRequest(
        "missing-channel", 0, "updateChannelSweep", std::move(payload)));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::UnprocessableContent_422);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "validationError");
    EXPECT_EQ(body.at("errorCode"), "channel-not-found");
}

TEST_F(WebApiErrorTest, MapsMissingMeasurementToStableErrorCode) {
    const auto response = postCommand(commandRequest(
        "missing-measurement",
        0,
        "createTrace",
        {{"windowId", 99}, {"measurementId", 99}, {"format", "phase"}}));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::UnprocessableContent_422);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "validationError");
    EXPECT_EQ(body.at("errorCode"), "measurement-not-found");
}

TEST_F(WebApiErrorTest, MapsMissingWindowToStableErrorCode) {
    const auto channel = postCommand(commandRequest(
        "create-channel", 0, "createChannel", sweepPayload()));
    ASSERT_TRUE(channel);
    ASSERT_EQ(channel->status, httplib::StatusCode::OK_200);
    const auto measurement = postCommand(commandRequest(
        "create-measurement",
        1,
        "createMeasurement",
        {{"channelId", 1}, {"type", "S11"}}));
    ASSERT_TRUE(measurement);
    ASSERT_EQ(measurement->status, httplib::StatusCode::OK_200);

    const auto response = postCommand(commandRequest(
        "missing-window",
        2,
        "createTrace",
        {{"windowId", 99}, {"measurementId", 1}, {"format", "smith"}}));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::UnprocessableContent_422);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "validationError");
    EXPECT_EQ(body.at("errorCode"), "window-not-found");
}

TEST_F(WebApiErrorTest, MapsMissingTraceToStableErrorCode) {
    const auto response = postCommand(commandRequest(
        "missing-trace",
        0,
        "updateTraceFormat",
        {{"traceId", 99}, {"format", "smith"}}));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::UnprocessableContent_422);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "validationError");
    EXPECT_EQ(body.at("errorCode"), "trace-not-found");
}

}  // namespace
}  // namespace vna::web_api
