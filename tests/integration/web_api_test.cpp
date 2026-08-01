#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <thread>
#include <utility>

#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

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

nlohmann::json commandRequest(
    std::string commandId,
    std::uint64_t revision,
    std::string type,
    nlohmann::json payload) {
    return {
        {"commandId", std::move(commandId)},
        {"sessionId", "session-1"},
        {"instrumentId", "instrument-1"},
        {"expectedStateRevision", revision},
        {"type", std::move(type)},
        {"payload", std::move(payload)},
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

    httplib::Result postCommand(const nlohmann::json& request) const {
        httplib::Client client{"127.0.0.1", port_};
        return client.Post(
            "/api/v1/commands", request.dump(), "application/json");
    }
    application::OperationManager operations_;
    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler()};
    application::TraceDisplayFrameRepository repository_{1};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{commandBus_, operations_, query_, display_model::TraceId{1}};
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
    ASSERT_EQ(postCommand(createChannelRequest())->status, 200);
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

TEST_F(WebApiTest, CreatesAndUpdatesTraceThroughHttp) {
    ASSERT_EQ(postCommand(createChannelRequest())->status, 200);

    const auto measurement = postCommand(commandRequest(
        "command-2", 1, "createMeasurement", {{"channelId", 1}, {"type", "S11"}}));
    ASSERT_TRUE(measurement);
    ASSERT_EQ(measurement->status, 200);
    EXPECT_EQ(nlohmann::json::parse(measurement->body)["value"]["measurementId"], 1);

    const auto window = postCommand(commandRequest(
        "command-3", 2, "createWindow", nlohmann::json::object()));
    ASSERT_TRUE(window);
    ASSERT_EQ(window->status, 200);
    EXPECT_EQ(nlohmann::json::parse(window->body)["value"]["windowId"], 1);

    const auto trace = postCommand(commandRequest(
        "command-4", 3, "createTrace",
        {{"windowId", 1}, {"measurementId", 1}, {"format", "logMagnitude"}}));
    ASSERT_TRUE(trace);
    ASSERT_EQ(trace->status, 200);
    EXPECT_EQ(nlohmann::json::parse(trace->body)["value"]["traceId"], 1);

    const auto updated = postCommand(commandRequest(
        "command-5",
        4,
        "updateTraceFormat",
        {{"traceId", 1}, {"format", "phase"}}));
    ASSERT_TRUE(updated);
    EXPECT_EQ(updated->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(nlohmann::json::parse(updated->body)["stateRevision"], 5);
    const auto invalid = postCommand(commandRequest(
        "command-6", 5, "updateTraceFormat", {{"traceId", 1}, {"format", "polar"}}));
    ASSERT_TRUE(invalid);
    EXPECT_EQ(invalid->status, httplib::StatusCode::BadRequest_400);
    EXPECT_EQ(nlohmann::json::parse(invalid->body)["error"], "invalidCommand");
    const auto missing = postCommand(commandRequest(
        "command-7", 5, "updateTraceFormat", {{"traceId", 99}, {"format", "smith"}}));
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, httplib::StatusCode::UnprocessableContent_422);
    EXPECT_EQ(nlohmann::json::parse(missing->body)["status"], "validationError");
    EXPECT_EQ(nlohmann::json::parse(missing->body)["stateRevision"], 5);

    httplib::Client client{"127.0.0.1", port_};
    const auto state = client.Get("/api/v1/state");
    ASSERT_TRUE(state);
    const auto instrument = nlohmann::json::parse(state->body).at("instrument");
    EXPECT_EQ(instrument.at("windows").size(), 1U);
    ASSERT_EQ(instrument.at("traces").size(), 1U);
    EXPECT_EQ(instrument.at("traces").at(0).at("format"), "phase");
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

TEST_F(WebApiTest, UpdatesChannelSweepThroughHttp) {
    ASSERT_EQ(postCommand(createChannelRequest())->status, 200);
    auto request = commandRequest(
        "command-2",
        1,
        "updateChannelSweep",
        {{"channelId", 1},
         {"startFrequencyHz", 100'000'000},
         {"stopFrequencyHz", 6'000'000'000},
         {"points", 401},
         {"ifBandwidthHz", 1'000},
         {"powerDbm", -5.0}});
    request["payload"]["startFrequencyHz"] = -2;
    EXPECT_EQ(postCommand(request)->status, httplib::StatusCode::BadRequest_400);
    EXPECT_EQ(commandBus_.snapshot().stateRevision, 1U);
    request["payload"]["startFrequencyHz"] = 100'000'000;
    const auto response = postCommand(request);
    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(nlohmann::json::parse(response->body)["stateRevision"], 2);
    const auto sweep = commandBus_.snapshot().instrument.channels[0].sweep;
    EXPECT_EQ(sweep.startFrequencyHz, 100'000'000U);
    EXPECT_EQ(sweep.stopFrequencyHz, 6'000'000'000U);
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
