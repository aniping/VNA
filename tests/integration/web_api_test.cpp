#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

application::CommandEnvelope createChannelCommand() {
    return {
        .commandId = application::CommandId{"command-1"},
        .sessionId = application::SessionId{"session-1"},
        .instrumentId = application::InstrumentId{"instrument-1"},
        .expectedStateRevision = 0,
        .timeout = std::chrono::seconds{5},
        .priority = application::CommandPriority::Normal,
        .payload = application::CreateChannelCommand{domain::SweepSettings{
            .startFrequencyHz = 10'000'000,
            .stopFrequencyHz = 26'500'000'000,
            .points = 201,
            .ifBandwidthHz = 10'000,
            .powerDbm = -10.0,
        }},
    };
}

nlohmann::json createChannelRequest() {
    return {
        {"commandId", "command-1"},
        {"sessionId", "session-1"},
        {"instrumentId", "instrument-1"},
        {"expectedStateRevision", 0},
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

class WebApiTest : public ::testing::Test {
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

    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"}};
    WebApi webApi_{commandBus_};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiTest, ReportsHealthOverHttp) {
    httplib::Client client{"127.0.0.1", port_};

    const auto response = client.Get("/api/v1/health");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(response->get_header_value("Content-Type"), "application/json");
    EXPECT_EQ(response->body, R"({"status":"ok"})");
}

TEST_F(WebApiTest, ReturnsCurrentStateSnapshot) {
    ASSERT_EQ(
        commandBus_.dispatch(createChannelCommand()).status,
        application::CommandStatus::Succeeded);
    httplib::Client client{"127.0.0.1", port_};

    const auto response = client.Get("/api/v1/state");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("stateRevision"), 1);
    const auto& instrument = body.at("instrument");
    ASSERT_EQ(instrument.at("channels").size(), 1U);
    const auto& channel = instrument.at("channels").at(0);
    EXPECT_EQ(channel.at("id"), 1);
    EXPECT_EQ(channel.at("sweep").at("stopFrequencyHz"), 26'500'000'000);
    EXPECT_TRUE(instrument.at("measurements").empty());
    EXPECT_TRUE(instrument.at("windows").empty());
    EXPECT_TRUE(instrument.at("traces").empty());
}

TEST_F(WebApiTest, CreatesChannelThroughUnifiedCommandEndpoint) {
    httplib::Client client{"127.0.0.1", port_};

    const auto response = client.Post(
        "/api/v1/commands",
        createChannelRequest().dump(),
        "application/json");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "succeeded");
    EXPECT_EQ(body.at("stateRevision"), 1);
    EXPECT_EQ(body.at("value").at("channelId"), 1);
    EXPECT_EQ(commandBus_.snapshot().instrument.channels.size(), 1U);
}

TEST_F(WebApiTest, MapsStaleRevisionToConflict) {
    httplib::Client client{"127.0.0.1", port_};
    auto request = createChannelRequest();
    const auto created =
        client.Post("/api/v1/commands", request.dump(), "application/json");
    ASSERT_TRUE(created);
    ASSERT_EQ(created->status, httplib::StatusCode::OK_200);
    request["commandId"] = "command-2";

    const auto response = client.Post(
        "/api/v1/commands", request.dump(), "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::Conflict_409);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("status"), "conflict");
    EXPECT_EQ(body.at("stateRevision"), 1);
}

TEST_F(WebApiTest, MapsInvalidSweepToUnprocessableContent) {
    httplib::Client client{"127.0.0.1", port_};
    auto request = createChannelRequest();
    request["payload"]["startFrequencyHz"] = 30'000'000'000;

    const auto response = client.Post(
        "/api/v1/commands", request.dump(), "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(
        response->status,
        httplib::StatusCode::UnprocessableContent_422);
    EXPECT_EQ(commandBus_.snapshot().stateRevision, 0U);
}

TEST_F(WebApiTest, RejectsMalformedCommandJson) {
    httplib::Client client{"127.0.0.1", port_};

    const auto response =
        client.Post("/api/v1/commands", "{", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::BadRequest_400);
    EXPECT_EQ(commandBus_.snapshot().stateRevision, 0U);
}

}  // namespace
}  // namespace vna::web_api
