#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <future>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/operation_manager.hpp>
#include <vna/application/single_sweep_command_handler.hpp>
#include <vna/application/single_sweep_executor.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/simulation/simulation_sweep.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;

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
        : executor_(
              2,
              [](const frames::FrequencyAxis& axis, std::stop_token) {
                  return simulation::simulateSweep(axis);
              },
              operations_,
              repository_),
          handler_([this](application::SingleSweepWorkItem work) {
              return executor_.submit(std::move(work));
          }),
          commandBus_(application::InstrumentId{"instrument-1"}, handler_),
          query_(commandBus_, repository_),
          webApi_(commandBus_, operations_, query_) {}

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

    void waitForSessionOperations() {
        auto completion = std::promise<void>{};
        auto completed = completion.get_future();
        auto subscription = operations_.subscribe(
            operations_.captureFence(application::SessionId{"session-1"}),
            [&completion] { completion.set_value(); });
        ASSERT_EQ(completed.wait_for(2s), std::future_status::ready);
    }

    application::OperationManager operations_;
    application::TraceDisplayFrameRepository repository_{1};
    application::SingleSweepExecutor executor_;
    application::SingleSweepCommandHandler handler_;
    application::CommandBus commandBus_;
    application::TraceDisplayFrameQuery query_;
    WebApi webApi_;
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiSingleSweepTest, StartsRealSweepAndPublishesDisplayFrame) {
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

    waitForSessionOperations();
    const auto operation = get(
        "/api/v1/operations/" + std::to_string(operationId));
    const auto frame = get("/api/v1/traces/1/display-frame");
    ASSERT_TRUE(operation);
    ASSERT_TRUE(frame);
    ASSERT_EQ(operation->status, httplib::StatusCode::OK_200);
    ASSERT_EQ(frame->status, httplib::StatusCode::OK_200);
    const auto operationBody = Json::parse(operation->body);
    const auto frameBody = Json::parse(frame->body);
    EXPECT_EQ(operationBody.at("status"), "Succeeded");
    EXPECT_EQ(operationBody.at("frameId"), frameBody.at("frameId"));
    EXPECT_EQ(frameBody.at("traceId"), 1);
    EXPECT_EQ(frameBody.at("stateRevision"), 4);
    EXPECT_EQ(frameBody.at("frequenciesHz").size(), 5U);
    EXPECT_EQ(frameBody.at("values").size(), 5U);
}

TEST_F(WebApiSingleSweepTest, MapsUnsupportedSweepConfiguration) {
    configureTrace("S21");

    const auto response = post(commandRequest(
        "start-sweep", 4, "startSingleSweep", {{"channelId", 1}}));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::UnprocessableContent_422);
    const auto body = Json::parse(response->body);
    EXPECT_EQ(body.at("status"), "validationError");
    EXPECT_EQ(body.at("stateRevision"), 4);
    EXPECT_EQ(body.at("errorCode"), "unsupported-sweep-configuration");
    EXPECT_FALSE(repository_.latest(display_model::TraceId{1}));
}

}  // namespace
}  // namespace vna::web_api
