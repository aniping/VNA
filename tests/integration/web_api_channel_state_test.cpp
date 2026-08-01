#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <thread>

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

    Json postCreateChannel() const {
        httplib::Client client{"127.0.0.1", port_};
        const auto request = createChannelRequest().dump();
        const auto response = client.Post(
            "/api/v1/commands", request, "application/json");
        EXPECT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
        return Json::parse(response->body);
    }

    application::OperationManager operations_;
    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler(),
        application::makeFactoryPreset().commandBusState};
    application::TraceDisplayFrameRepository repository_{2};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{
        commandBus_, operations_, query_, display_model::TraceId{1}};
    int port_{-1};
    std::thread serverThread_;
};

void expectContinuousWithNoTrigger(const Json& channel) {
    EXPECT_EQ(channel.at("sweepMode"), "continuous");
    EXPECT_EQ(channel.at("triggerSource"), "none");
}

TEST_F(WebApiChannelStateTest, FactoryPresetExposesChannelStateAtRevisionZero) {
    const auto state = getState();

    EXPECT_EQ(state.at("stateRevision"), 0U);
    const auto& channels = state.at("instrument").at("channels");
    ASSERT_EQ(channels.size(), 1U);
    expectContinuousWithNoTrigger(channels.at(0));
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
