#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <thread>
#include <utility>

#include <vna/application/factory_preset.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

using Json = nlohmann::json;

Json createChannelRequest() {
    return {
        {"commandId", "create-channel-trigger-state"},
        {"sessionId", "channel-state-test"},
        {"instrumentId", "instrument-1"},
        {"expectedStateRevision", 0},
        {"type", "createChannel"},
        {"payload",
         {
             {"startFrequencyHz", 20'000'000},
             {"stopFrequencyHz", 1'000'000'000},
             {"points", 101},
             {"ifBandwidthHz", 1'000},
             {"powerDbm", -20.0},
         }},
    };
}

Json updateSweepControlRequest(
    std::string commandId,
    std::uint64_t revision,
    std::string mode,
    std::uint32_t sweepCount) {
    return {
        {"commandId", std::move(commandId)},
        {"sessionId", "channel-state-test"},
        {"instrumentId", "instrument-1"},
        {"expectedStateRevision", revision},
        {"type", "updateChannelSweepControl"},
        {"payload",
         {{"channelId", 1},
          {"mode", std::move(mode)},
          {"sweepCount", sweepCount}}},
    };
}

class WebApiChannelStateTest : public ::testing::Test {
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

    Json getState() const {
        httplib::Client client{"127.0.0.1", port_};
        const auto response = client.Get("/api/v1/state");
        EXPECT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
        return Json::parse(response->body);
    }

    httplib::Result postRaw(const Json& command) const {
        httplib::Client client{"127.0.0.1", port_};
        return client.Post(
            "/api/v1/commands", command.dump(), "application/json");
    }

    Json postCommand(const Json& command) const {
        const auto response = postRaw(command);
        EXPECT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
        return Json::parse(response->body);
    }

    Json postCreateChannel() const {
        return postCommand(createChannelRequest());
    }

    application::OperationManager operations_;
    vna::test::StoppedCommandBus commandBus_{
        application::InstrumentId{"instrument-1"},
        application::makeFactoryPreset().commandBusState};
    application::TraceDisplayFrameRepository repository_{2};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{
        commandBus_, operations_, query_, repository_};
    int port_{-1};
    std::thread serverThread_;
};

void expectContinuousWithNoTrigger(const Json& channel) {
    EXPECT_EQ(channel.at("sweepMode"), "continuous");
    EXPECT_EQ(channel.at("sweepCount"), 1U);
    EXPECT_EQ(channel.at("triggerSource"), "none");
}

TEST_F(WebApiChannelStateTest, FactoryPresetExposesChannelStateAtRevisionZero) {
    const auto state = getState();

    EXPECT_EQ(state.at("stateRevision"), 0U);
    const auto& channels = state.at("instrument").at("channels");
    ASSERT_EQ(channels.size(), 1U);
    expectContinuousWithNoTrigger(channels.at(0));
    EXPECT_EQ(state.at("sweepRuntime").at("state"), "running");
    EXPECT_TRUE(state.at("sweepRuntime").at("phase").is_string());
    EXPECT_EQ(
        state.at("sweepRuntime").at("configured"),
        Json({
            {"stateRevision", 0},
            {"mode", "continuous"},
            {"sweepCount", 1},
        }));
    EXPECT_EQ(
        state.at("sweepRuntime").at("applied"),
        Json({
            {"stateRevision", 0},
            {"generation", 1},
            {"mode", "continuous"},
            {"sweepCount", 1},
        }));
}

TEST_F(WebApiChannelStateTest, SingleModeUsesStableWireName) {
    const auto request =
        updateSweepControlRequest("single-mode", 0, "single", 3);
    const auto updated = postCommand(request);
    ASSERT_EQ(updated.at("status"), "succeeded");
    ASSERT_EQ(updated.at("stateRevision"), 1U);
    EXPECT_EQ(postCommand(request), updated);

    const auto state = getState();
    const auto& channel = state.at("instrument").at("channels").at(0);
    EXPECT_EQ(channel.at("sweepMode"), "single");
    EXPECT_EQ(channel.at("sweepCount"), 3U);
    EXPECT_EQ(
        state.at("sweepRuntime").at("configured"),
        Json({
            {"stateRevision", 1},
            {"mode", "single"},
            {"sweepCount", 3},
        }));
    EXPECT_EQ(
        state.at("sweepRuntime").at("applied"),
        Json({
            {"stateRevision", 0},
            {"generation", 1},
            {"mode", "continuous"},
            {"sweepCount", 1},
        }));
}

TEST_F(WebApiChannelStateTest, RejectsInvalidSweepControlWire) {
    const auto invalidMode =
        postRaw(updateSweepControlRequest("invalid-mode", 0, "hold", 1));
    ASSERT_TRUE(invalidMode);
    EXPECT_EQ(invalidMode->status, httplib::StatusCode::BadRequest_400);
    EXPECT_EQ(Json::parse(invalidMode->body).at("error"), "invalidCommand");

    auto missingCount =
        updateSweepControlRequest("missing-count", 0, "single", 1);
    missingCount.at("payload").erase("sweepCount");
    const auto missing = postRaw(missingCount);
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, httplib::StatusCode::BadRequest_400);

    const auto invalidCount =
        postRaw(updateSweepControlRequest("zero-count", 0, "single", 0));
    ASSERT_TRUE(invalidCount);
    EXPECT_EQ(invalidCount->status, httplib::StatusCode::UnprocessableContent_422);
    EXPECT_EQ(
        Json::parse(invalidCount->body).at("errorCode"),
        "invalid-sweep-settings");
    EXPECT_EQ(getState().at("stateRevision"), 0U);
}

TEST_F(WebApiChannelStateTest, CommandCreatedChannelUsesSupportedState) {
    const auto result = postCreateChannel();

    EXPECT_EQ(result.at("status"), "succeeded");
    EXPECT_EQ(result.at("stateRevision"), 1U);
    const auto state = getState();
    EXPECT_EQ(state.at("stateRevision"), 1U);
    const auto& channels = state.at("instrument").at("channels");
    ASSERT_EQ(channels.size(), 2U);
    expectContinuousWithNoTrigger(channels.at(1));
}

}  // namespace
}  // namespace vna::web_api
