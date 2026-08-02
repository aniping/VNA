#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <string>
#include <thread>

#include <vna/application/operation_manager.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/test/captured_runtime_log.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

nlohmann::json commandRequest(
    std::string commandId,
    std::uint64_t revision,
    std::string type,
    nlohmann::json payload) {
    return {
        {"commandId", std::move(commandId)},
        {"sessionId", "web-log-session"},
        {"instrumentId", "instrument-1"},
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

nlohmann::json createChannelRequest(
    std::string commandId,
    std::uint64_t revision) {
    return commandRequest(
        std::move(commandId), revision, "createChannel", sweepPayload());
}

class WebApiBusinessLogTest : public ::testing::Test {
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

    httplib::Result post(const nlohmann::json& request) const {
        httplib::Client client{"127.0.0.1", port_};
        return client.Post(
            "/api/v1/commands", request.dump(), "application/json");
    }

    vna::test::CapturedRuntimeLog log_;
    application::OperationManager operations_;
    vna::test::StoppedCommandBus commandBus_{
        application::InstrumentId{"instrument-1"}};
    application::TraceDisplayFrameRepository repository_{2};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{commandBus_, operations_, query_, repository_};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiBusinessLogTest, LogsOnlyDecodedBusinessCommandResults) {
    httplib::Client client{"127.0.0.1", port_};
    ASSERT_TRUE(client.Get("/api/v1/state"));
    ASSERT_TRUE(client.Post("/api/v1/commands", "{", "application/json"));
    EXPECT_TRUE(log_.text().empty());

    const auto accepted = createChannelRequest("web-accepted", 0);
    ASSERT_EQ(post(accepted)->status, httplib::StatusCode::OK_200);
    ASSERT_EQ(post(accepted)->status, httplib::StatusCode::OK_200);
    ASSERT_EQ(
        post(createChannelRequest("web-rejected", 0))->status,
        httplib::StatusCode::Conflict_409);

    const auto text = log_.text();
    const auto success =
        "INFO [配置命令] 创建通道请求已成功处理 | command_id=web-accepted "
        "| session_id=web-log-session | instrument_id=instrument-1 | "
        "revision=1 | channel_id=1";
    EXPECT_EQ(log_.count(success), 2U);
    EXPECT_NE(
        text.find(
            "WARN [配置命令] 创建通道请求被拒绝 | "
            "command_id=web-rejected | session_id=web-log-session | "
            "instrument_id=instrument-1 | revision=1 | "
            "error_code=state-revision-conflict"),
        std::string::npos);
    EXPECT_EQ(text.find("/api/v1/state"), std::string::npos);
    EXPECT_EQ(text.find("invalidCommand"), std::string::npos);
}

TEST_F(WebApiBusinessLogTest, LogsDisabledSingleSweepAsResourceBusy) {
    ASSERT_EQ(post(createChannelRequest("channel", 0))->status, 200);
    ASSERT_EQ(post(commandRequest(
        "measurement", 1, "createMeasurement",
        {{"channelId", 1}, {"type", "S11"}}))->status, 200);
    ASSERT_EQ(post(commandRequest(
        "window", 2, "createWindow", nlohmann::json::object()))->status, 200);
    ASSERT_EQ(post(commandRequest(
        "trace", 3, "createTrace",
        {{"windowId", 1}, {"measurementId", 1},
         {"format", "logMagnitude"}}))->status, 200);
    log_.clear();

    const auto response = post(commandRequest(
        "disabled-sweep", 4, "startSingleSweep", {{"channelId", 1}}));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::Conflict_409);
    const auto text = log_.text();
    EXPECT_NE(text.find(
        "WARN [单次扫频] 启动通道#1单次扫频请求被拒绝 | "
        "command_id=disabled-sweep | session_id=web-log-session | "
        "instrument_id=instrument-1 | revision=4 | "
        "error_code=resource-busy"), std::string::npos);
    EXPECT_EQ(text.find("operation_id="), std::string::npos);
    EXPECT_EQ(text.find("sweep_id="), std::string::npos);
    EXPECT_EQ(text.find("frame_id="), std::string::npos);
}

}  // namespace
}  // namespace vna::web_api
