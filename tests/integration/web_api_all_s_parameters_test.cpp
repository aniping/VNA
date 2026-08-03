#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <vna/application/factory_preset.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

using Json = nlohmann::json;

Json commandRequest(
    std::string commandId,
    std::uint64_t revision,
    Json payload) {
    return {
        {"commandId", std::move(commandId)},
        {"sessionId", "all-s-parameters-test"},
        {"instrumentId", "instrument-1"},
        {"expectedStateRevision", revision},
        {"type", "ensureAllSParameters"},
        {"payload", std::move(payload)},
    };
}

const Json& findMeasurement(const Json& instrument, std::uint64_t id) {
    for (const auto& measurement : instrument.at("measurements")) {
        if (measurement.at("id") == id) {
            return measurement;
        }
    }
    throw std::runtime_error{"measurement missing from state"};
}

class WebApiAllSParametersTest : public ::testing::Test {
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

    httplib::Result post(const Json& request) const {
        httplib::Client client{"127.0.0.1", port_};
        return client.Post(
            "/api/v1/commands", request.dump(), "application/json");
    }

    Json state() const {
        httplib::Client client{"127.0.0.1", port_};
        const auto response = client.Get("/api/v1/state");
        if (!response) {
            throw std::runtime_error{"state request failed"};
        }
        EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
        return Json::parse(response->body);
    }

    application::FactoryPreset preset_{application::makeFactoryPreset()};
    application::OperationManager operations_;
    vna::test::StoppedCommandBus commandBus_{
        application::InstrumentId{"instrument-1"}, preset_.commandBusState};
    application::TraceDisplayFrameRepository repository_{4};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{
        commandBus_, operations_, query_, {repository_, commandBus_.previews()}};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiAllSParametersTest, CreatesFourSingleTraceDiagramsAndReplays) {
    const auto request = commandRequest(
        "ensure-all", 0, {{"traceId", preset_.defaultTraceId.value()}});
    const auto first = post(request);
    const auto replay = post(request);

    ASSERT_TRUE(first);
    ASSERT_TRUE(replay);
    ASSERT_EQ(first->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(replay->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(replay->body, first->body);
    const auto result = Json::parse(first->body);
    EXPECT_EQ(result.at("stateRevision"), 1);
    EXPECT_EQ(result.at("value").at("traceId"), 1);

    const auto noOp = post(commandRequest(
        "ensure-complete", 1, {{"traceId", preset_.defaultTraceId.value()}}));
    ASSERT_TRUE(noOp);
    EXPECT_EQ(noOp->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(Json::parse(noOp->body).at("stateRevision"), 1);

    const auto snapshot = state();
    const auto& instrument = snapshot.at("instrument");
    EXPECT_EQ(snapshot.at("stateRevision"), 1);
    EXPECT_EQ(instrument.at("channels").size(), 1U);
    EXPECT_EQ(instrument.at("measurements").size(), 4U);
    EXPECT_EQ(instrument.at("windows").size(), 4U);
    EXPECT_EQ(instrument.at("traces").size(), 4U);

    constexpr std::array expectedTypes{"S11", "S12", "S21", "S22"};
    for (std::size_t index = 0; index < expectedTypes.size(); ++index) {
        const auto expectedId = static_cast<std::uint64_t>(index + 1);
        const auto& trace = instrument.at("traces").at(index);
        EXPECT_EQ(trace.at("id"), expectedId);
        EXPECT_EQ(trace.at("windowId"), expectedId);
        EXPECT_EQ(trace.at("format"), "logMagnitude");
        const auto measurementId =
            trace.at("measurementId").get<std::uint64_t>();
        EXPECT_EQ(
            findMeasurement(instrument, measurementId).at("type"),
            expectedTypes[index]);
    }
}

TEST_F(WebApiAllSParametersTest, RejectsAliasAndMissingTraceWithoutMutation) {
    const auto alias = post(commandRequest(
        "alias", 0, {{"anchorTraceId", preset_.defaultTraceId.value()}}));
    ASSERT_TRUE(alias);
    EXPECT_EQ(alias->status, httplib::StatusCode::BadRequest_400);
    EXPECT_EQ(alias->body, R"({"error":"invalidCommand"})");

    const auto missing = post(commandRequest(
        "missing", 0, {{"traceId", 99}}));
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, httplib::StatusCode::UnprocessableContent_422);
    const auto error = Json::parse(missing->body);
    EXPECT_EQ(error.at("status"), "validationError");
    EXPECT_EQ(error.at("errorCode"), "trace-not-found");
    EXPECT_EQ(error.at("stateRevision"), 0);

    const auto snapshot = state();
    EXPECT_EQ(snapshot.at("stateRevision"), 0);
    EXPECT_EQ(snapshot.at("instrument").at("windows").size(), 1U);
    EXPECT_EQ(snapshot.at("instrument").at("traces").size(), 1U);
}

}  // namespace
}  // namespace vna::web_api
