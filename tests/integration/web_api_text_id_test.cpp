#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <string>
#include <thread>

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
         {{"startFrequencyHz", 10'000'000},
          {"stopFrequencyHz", 26'500'000'000},
          {"points", 201},
          {"ifBandwidthHz", 10'000},
          {"powerDbm", -10.0}}},
    };
}

void expectSameState(
    const application::StateSnapshot& before,
    const application::StateSnapshot& after) {
    EXPECT_EQ(after.stateRevision, before.stateRevision);
    EXPECT_EQ(
        after.instrument.channels.size(),
        before.instrument.channels.size());
    EXPECT_EQ(
        after.instrument.measurements.size(),
        before.instrument.measurements.size());
    EXPECT_EQ(after.display.windows.size(), before.display.windows.size());
    EXPECT_EQ(after.display.traces.size(), before.display.traces.size());
}

class WebApiTextIdTest : public ::testing::Test {
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

    void expectInvalidIdRejected(
        const std::string& field,
        const std::string& value) const {
        const auto before = commandBus_.snapshot();
        auto request = createChannelRequest();
        request[field] = value;

        const auto response = postCommand(request);

        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::BadRequest_400);
        EXPECT_EQ(response->body, R"({"error":"invalidCommand"})");
        expectSameState(before, commandBus_.snapshot());
    }

    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler()};
    application::TraceDisplayFrameRepository repository_{1};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{commandBus_, query_};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiTextIdTest, RejectsEmptyCommandIdWithoutChangingState) {
    expectInvalidIdRejected("commandId", "");
}

TEST_F(WebApiTextIdTest, RejectsOversizedSessionIdWithoutChangingState) {
    expectInvalidIdRejected("sessionId", std::string(129, 's'));
}

TEST_F(WebApiTextIdTest, RejectsControlByteInstrumentIdWithoutChangingState) {
    expectInvalidIdRejected("instrumentId", "instrument\n1");
}

}  // namespace
}  // namespace vna::web_api
