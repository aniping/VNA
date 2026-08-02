#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <thread>

#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

class WebApiStateCacheTest : public ::testing::Test {
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

    [[nodiscard]] httplib::Result getState() const {
        httplib::Client client{"127.0.0.1", port_};
        return client.Get("/api/v1/state");
    }

    [[nodiscard]] httplib::Result createChannel() const {
        const nlohmann::json request = {
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
        httplib::Client client{"127.0.0.1", port_};
        return client.Post(
            "/api/v1/commands", request.dump(), "application/json");
    }

    application::OperationManager operations_;
    vna::test::StoppedCommandBus commandBus_{
        application::InstrumentId{"instrument-1"}};
    application::TraceDisplayFrameRepository repository_{1};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{commandBus_, operations_, query_, repository_};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiStateCacheTest, SuccessfulStateResponsesDisableCaching) {
    const auto initial = getState();
    ASSERT_TRUE(initial);
    ASSERT_EQ(initial->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(initial->get_header_value("Cache-Control"), "no-store");
    EXPECT_EQ(nlohmann::json::parse(initial->body).at("stateRevision"), 0);

    const auto command = createChannel();
    ASSERT_TRUE(command);
    ASSERT_EQ(command->status, httplib::StatusCode::OK_200);

    const auto changed = getState();
    ASSERT_TRUE(changed);
    ASSERT_EQ(changed->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(changed->get_header_value("Cache-Control"), "no-store");
    EXPECT_EQ(nlohmann::json::parse(changed->body).at("stateRevision"), 1);
}

}  // namespace
}  // namespace vna::web_api
