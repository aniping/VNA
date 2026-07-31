#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <array>
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

void expectValidationError(
    const httplib::Result& response,
    const char* errorCode,
    std::uint64_t revision) {
    ASSERT_TRUE(response);
    ASSERT_EQ(response->status,
              httplib::StatusCode::UnprocessableContent_422);
    const auto body = Json::parse(response->body);
    EXPECT_EQ(body.at("status"), "validationError");
    EXPECT_EQ(body.at("stateRevision"), revision);
    EXPECT_EQ(body.at("errorCode"), errorCode);
}

void expectScale(
    const Json& scale,
    double scalePerDivision,
    double minimum,
    double maximum) {
    EXPECT_EQ(scale.at("scalePerDivision"), scalePerDivision);
    EXPECT_EQ(scale.at("referenceValue"), 0.0);
    EXPECT_EQ(scale.at("referencePosition"), 8.0);
    EXPECT_EQ(scale.at("minimum"), minimum);
    EXPECT_EQ(scale.at("maximum"), maximum);
    EXPECT_EQ(scale.at("unit"), "dB");
}

class WebApiScaleTest : public ::testing::Test {
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
        return postRaw(request.dump());
    }

    httplib::Result postRaw(const std::string& body) const {
        httplib::Client client{"127.0.0.1", port_};
        return client.Post("/api/v1/commands", body, "application/json");
    }

    Json getState() const {
        httplib::Client client{"127.0.0.1", port_};
        const auto response = client.Get("/api/v1/state");
        EXPECT_TRUE(response);
        if (!response) {
            return Json::object();
        }
        EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
        return Json::parse(response->body);
    }

    Json postSucceeded(
        std::string commandId,
        std::uint64_t revision,
        std::string type,
        Json payload) const {
        const auto response = postCommand(commandRequest(
            std::move(commandId), revision, std::move(type), std::move(payload)));
        EXPECT_TRUE(response);
        if (!response) {
            return Json::object();
        }
        EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
        return Json::parse(response->body);
    }

    void createDisplayBase() {
        postSucceeded("create-channel", 0, "createChannel", sweepPayload());
        postSucceeded("create-measurement", 1, "createMeasurement",
                      {{"channelId", 1}, {"type", "S11"}});
        postSucceeded("create-window", 2, "createWindow", Json::object());
        revision_ = 3;
    }
    std::uint64_t createTrace(const std::string& format) {
        const auto body = postSucceeded(
            "create-trace-" + std::to_string(revision_),
            revision_,
            "createTrace",
            {{"windowId", 1}, {"measurementId", 1}, {"format", format}});
        ++revision_;
        return body.at("value").at("traceId").get<std::uint64_t>();
    }
    application::CommandBus commandBus_{application::InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    application::TraceDisplayFrameRepository repository_{1};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{commandBus_, query_};
    int port_{-1};
    std::thread serverThread_;
    std::uint64_t revision_{0};
};

TEST_F(WebApiScaleTest, UpdatesScaleAndReturnsTargetIsolatedState) {
    createDisplayBase();
    const auto targetTraceId = createTrace("logMagnitude");
    const auto otherTraceId = createTrace("logMagnitude");

    const auto response = postCommand(commandRequest(
        "update-scale",
        revision_,
        "updateTraceScalePerDivision",
        {{"traceId", targetTraceId}, {"scalePerDivision", 5.0}}));

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
    const auto body = Json::parse(response->body);
    EXPECT_EQ(body.at("status"), "succeeded");
    EXPECT_EQ(body.at("stateRevision"), revision_ + 1);
    EXPECT_EQ(body.at("value").at("traceId"), targetTraceId);

    const auto state = getState();
    EXPECT_EQ(state.at("stateRevision"), revision_ + 1);
    const auto& instrument = state.at("instrument");
    EXPECT_EQ(instrument.at("channels").size(), 1U);
    EXPECT_EQ(instrument.at("measurements").size(), 1U);
    EXPECT_EQ(instrument.at("windows").size(), 1U);
    ASSERT_EQ(instrument.at("traces").size(), 2U);
    const auto& traces = instrument.at("traces");
    EXPECT_EQ(traces.at(0).at("id"), targetTraceId);
    EXPECT_EQ(traces.at(1).at("id"), otherTraceId);
    expectScale(traces.at(0).at("scale"), 5.0, -40.0, 10.0);
    expectScale(traces.at(1).at("scale"), 10.0, -80.0, 20.0);
}

TEST_F(WebApiScaleTest, MapsMissingTraceToValidationError) {
    const auto response = postCommand(commandRequest(
        "missing-trace", 0, "updateTraceScalePerDivision",
        {{"traceId", 99}, {"scalePerDivision", 5.0}}));

    expectValidationError(response, "trace-not-found", 0);
}

TEST_F(WebApiScaleTest, RejectsNonPositiveScalePerDivision) {
    createDisplayBase();
    const auto traceId = createTrace("logMagnitude");

    for (const auto value : {0.0, -1.0}) {
        const auto response = postCommand(commandRequest(
            value == 0.0 ? "invalid-scale-zero" : "invalid-scale-negative",
            revision_,
            "updateTraceScalePerDivision",
            {{"traceId", traceId}, {"scalePerDivision", value}}));
        expectValidationError(response, "invalid-scale-per-division", revision_);
    }

    const auto state = getState();
    EXPECT_EQ(state.at("stateRevision"), revision_);
    expectScale(state.at("instrument").at("traces").at(0).at("scale"),
                10.0, -80.0, 20.0);
}

TEST_F(WebApiScaleTest, RejectsPhaseAndSmithAndSerializesNullScale) {
    createDisplayBase();
    const auto phaseId = createTrace("phase");
    const auto smithId = createTrace("smith");

    for (const auto traceId : {phaseId, smithId}) {
        const auto response = postCommand(commandRequest(
            "unsupported-scale-" + std::to_string(traceId),
            revision_,
            "updateTraceScalePerDivision",
            {{"traceId", traceId}, {"scalePerDivision", 5.0}}));
        expectValidationError(response, "scale-not-supported-for-format", revision_);
    }

    const auto state = getState();
    EXPECT_EQ(state.at("stateRevision"), revision_);
    const auto& traces = state.at("instrument").at("traces");
    ASSERT_EQ(traces.size(), 2U);
    EXPECT_TRUE(traces.at(0).at("scale").is_null());
    EXPECT_TRUE(traces.at(1).at("scale").is_null());
}

TEST_F(WebApiScaleTest, RejectsMalformedMissingAndNonNumberPayloads) {
    const std::array<std::string, 3> requests{
        "{",
        commandRequest("missing-field", 0, "updateTraceScalePerDivision",
                       {{"traceId", 1}})
            .dump(),
        commandRequest("non-number", 0, "updateTraceScalePerDivision",
                       {{"traceId", 1}, {"scalePerDivision", "five"}})
            .dump(),
    };

    for (const auto& request : requests) {
        const auto response = postRaw(request);
        ASSERT_TRUE(response);
        ASSERT_EQ(response->status, httplib::StatusCode::BadRequest_400);
        EXPECT_EQ(response->body, R"({"error":"invalidCommand"})");
    }
    EXPECT_EQ(getState().at("stateRevision"), 0);
}
}  // namespace
}  // namespace vna::web_api
