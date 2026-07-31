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

using Json = nlohmann::json;

Json commandRequest(
    std::string commandId,
    std::uint64_t revision,
    std::string type,
    Json payload) {
    return {
        {"commandId", std::move(commandId)},
        {"sessionId", "session-1"},
        {"instrumentId", "instrument-1"},
        {"expectedStateRevision", revision},
        {"type", std::move(type)},
        {"payload", std::move(payload)},
    };
}

Json sweepPayload() {
    return {
        {"startFrequencyHz", 10'000'000},
        {"stopFrequencyHz", 26'500'000'000},
        {"points", 201},
        {"ifBandwidthHz", 10'000},
        {"powerDbm", -10.0},
    };
}

class WebApiIdempotencyTest : public ::testing::Test {
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

    httplib::Result postCommand(const Json& request) const {
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
    WebApi webApi_{commandBus_, operations_, query_};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiIdempotencyTest, ReplaysFirstCompleteResponseOverHttp) {
    const auto request = commandRequest(
        "shared-command", 0, "createWindow", Json::object());

    const auto first = postCommand(request);
    const auto replay = postCommand(request);

    ASSERT_TRUE(first);
    ASSERT_TRUE(replay);
    ASSERT_EQ(first->status, httplib::StatusCode::OK_200);
    ASSERT_EQ(replay->status, httplib::StatusCode::OK_200);
    const auto firstBody = Json::parse(first->body);
    const auto replayBody = Json::parse(replay->body);
    EXPECT_EQ(replayBody, firstBody);
    EXPECT_EQ(replayBody.at("status"), "succeeded");
    EXPECT_EQ(replayBody.at("stateRevision"), 1U);
    EXPECT_EQ(replayBody.at("value").at("windowId"), 1U);
    const auto snapshot = commandBus_.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 1U);
    EXPECT_EQ(snapshot.display.windows.size(), 1U);
    EXPECT_EQ(commandBus_.stats().idempotencyEntries, 1U);
}

TEST_F(WebApiIdempotencyTest, RejectsReuseWithoutReplacingFirstResponse) {
    const auto original = commandRequest(
        "shared-command", 0, "createWindow", Json::object());
    const auto first = postCommand(original);
    ASSERT_TRUE(first);
    ASSERT_EQ(first->status, httplib::StatusCode::OK_200);
    const auto advanced = postCommand(commandRequest(
        "advance", 1, "createWindow", Json::object()));
    ASSERT_TRUE(advanced);
    ASSERT_EQ(advanced->status, httplib::StatusCode::OK_200);

    const auto reused = postCommand(commandRequest(
        "shared-command", 0, "createChannel", sweepPayload()));

    ASSERT_TRUE(reused);
    ASSERT_EQ(reused->status, httplib::StatusCode::Conflict_409);
    const auto reusedBody = Json::parse(reused->body);
    EXPECT_EQ(reusedBody.at("status"), "conflict");
    EXPECT_EQ(reusedBody.at("errorCode"), "command-id-reuse");
    EXPECT_EQ(reusedBody.at("stateRevision"), 2U);
    const auto replay = postCommand(original);
    ASSERT_TRUE(replay);
    ASSERT_EQ(replay->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(Json::parse(replay->body), Json::parse(first->body));

    const auto snapshot = commandBus_.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 2U);
    EXPECT_EQ(snapshot.instrument.channels.size(), 0U);
    EXPECT_EQ(snapshot.display.windows.size(), 2U);
    EXPECT_EQ(commandBus_.stats().idempotencyEntries, 2U);
    EXPECT_EQ(commandBus_.stats().idempotencyEvictions, 0U);
}

TEST_F(WebApiIdempotencyTest, InvalidIdDoesNotEnterIdempotencyWindow) {
    const auto before = commandBus_.snapshot();
    const auto response = postCommand(commandRequest(
        "", 0, "createWindow", Json::object()));

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::BadRequest_400);
    EXPECT_EQ(response->body, R"({"error":"invalidCommand"})");
    const auto after = commandBus_.snapshot();
    EXPECT_EQ(after.stateRevision, before.stateRevision);
    EXPECT_EQ(
        after.instrument.channels.size(),
        before.instrument.channels.size());
    EXPECT_EQ(
        after.instrument.measurements.size(),
        before.instrument.measurements.size());
    EXPECT_EQ(after.display.windows.size(), before.display.windows.size());
    EXPECT_EQ(after.display.traces.size(), before.display.traces.size());
    const auto stats = commandBus_.stats();
    EXPECT_EQ(stats.idempotencyEntries, 0U);
    EXPECT_EQ(stats.idempotencyEvictions, 0U);
}

}  // namespace
}  // namespace vna::web_api
