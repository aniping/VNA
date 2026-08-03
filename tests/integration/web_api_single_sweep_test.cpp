#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/operation_manager.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/application/trace_publication_catalog.hpp>
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
        {"startFrequencyHz", 1'000'000},
        {"stopFrequencyHz", 2'000'000},
        {"points", 5},
        {"ifBandwidthHz", 1'000},
        {"powerDbm", -10.0},
    };
}

class WebApiSingleSweepTest : public ::testing::Test {
protected:
    WebApiSingleSweepTest()
        : commandBus_(
              application::InstrumentId{"instrument-1"},
              runtimeOwner_.runtime()),
          query_(commandBus_, repository_),
          webApi_(
              commandBus_,
              operations_,
              query_,
              repository_) {}

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

    httplib::Result post(const Json& request) const {
        return httplib::Client{"127.0.0.1", port_}.Post(
            "/api/v1/commands", request.dump(), "application/json");
    }

    httplib::Result get(const std::string& path) const {
        return httplib::Client{"127.0.0.1", port_}.Get(path);
    }

    void configureTrace(const char* measurementType) {
        const auto channel = post(commandRequest(
            "create-channel", 0, "createChannel", sweepPayload()));
        ASSERT_TRUE(channel);
        ASSERT_EQ(channel->status, httplib::StatusCode::OK_200);
        const auto measurement = post(commandRequest(
            "create-measurement",
            1,
            "createMeasurement",
            {{"channelId", 1}, {"type", measurementType}}));
        ASSERT_TRUE(measurement);
        ASSERT_EQ(measurement->status, httplib::StatusCode::OK_200);
        const auto window = post(commandRequest(
            "create-window", 2, "createWindow", Json::object()));
        ASSERT_TRUE(window);
        ASSERT_EQ(window->status, httplib::StatusCode::OK_200);
        const auto trace = post(commandRequest(
            "create-trace",
            3,
            "createTrace",
            {{"windowId", 1},
             {"measurementId", 1},
             {"format", "logMagnitude"}}));
        ASSERT_TRUE(trace);
        ASSERT_EQ(trace->status, httplib::StatusCode::OK_200);
    }

    vna::test::CommandBusRuntimeOwner runtimeOwner_{{}, 1};
    application::OperationManager& operations_{runtimeOwner_.operations()};
    application::TraceDisplayFrameRepository& repository_{
        runtimeOwner_.repository()};
    application::CommandBus commandBus_;
    application::TraceDisplayFrameQuery query_;
    WebApi webApi_;
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiSingleSweepTest, RoutesRestartToRuntimeOperation) {
    configureTrace("S11");

    const auto accepted = post(commandRequest(
        "start-sweep", 4, "startSingleSweep", {{"channelId", 1}}));

    ASSERT_TRUE(accepted);
    ASSERT_EQ(accepted->status, httplib::StatusCode::OK_200);
    const auto body = Json::parse(accepted->body);
    EXPECT_EQ(body.at("status"), "succeeded");
    EXPECT_EQ(body.at("stateRevision"), 4);
    ASSERT_TRUE(body.at("value").contains("operationId"));
    const auto operationId =
        body.at("value").at("operationId").get<std::uint64_t>();
    EXPECT_GT(operationId, 0U);
    EXPECT_EQ(commandBus_.snapshot().stateRevision, 4U);

    const auto operation = get(
        "/api/v1/operations/" + std::to_string(operationId));
    ASSERT_TRUE(operation);
    ASSERT_EQ(operation->status, httplib::StatusCode::OK_200);
    const auto operationBody = Json::parse(operation->body);
    EXPECT_TRUE(operationBody.at("status") == "Queued" ||
                operationBody.at("status") == "Running");
    EXPECT_EQ(operationBody.at("submittedAtStateRevision"), 4);
}

TEST_F(WebApiSingleSweepTest, AcceptsDynamicS21Configuration) {
    configureTrace("S21");

    const auto response = post(commandRequest(
        "start-sweep", 4, "startSingleSweep", {{"channelId", 1}}));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
    const auto body = Json::parse(response->body);
    EXPECT_EQ(body.at("status"), "succeeded");
    EXPECT_EQ(body.at("stateRevision"), 4);
    EXPECT_GT(body.at("value").at("operationId").get<std::uint64_t>(), 0U);
}

}  // namespace
}  // namespace vna::web_api
