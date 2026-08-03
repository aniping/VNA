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
        {"sessionId", "trace-measurement-test"},
        {"instrumentId", "instrument-1"},
        {"expectedStateRevision", revision},
        {"type", "setTraceMeasurementType"},
        {"payload", std::move(payload)},
    };
}

class WebApiTraceMeasurementTest : public ::testing::Test {
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

const Json& findMeasurement(const Json& instrument, std::uint64_t id) {
    for (const auto& measurement : instrument.at("measurements")) {
        if (measurement.at("id") == id) {
            return measurement;
        }
    }
    throw std::runtime_error{"measurement missing from state"};
}

TEST_F(WebApiTraceMeasurementTest, SelectsEveryTwoPortSParameter) {
    const std::array<const char*, 4> types{"S11", "S12", "S21", "S22"};
    const auto initialWindowId = state()
                                     .at("instrument")
                                     .at("traces")
                                     .at(0)
                                     .at("windowId");
    std::uint64_t revision = 0;

    for (const auto* type : types) {
        const auto response = post(commandRequest(
            "set-" + std::string{type},
            revision,
            {{"traceId", preset_.defaultTraceId.value()},
             {"measurementType", type}}));

        ASSERT_TRUE(response);
        ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
        const auto body = Json::parse(response->body);
        EXPECT_EQ(body.at("status"), "succeeded");
        EXPECT_EQ(body.at("value").at("traceId"),
                  preset_.defaultTraceId.value());
        revision = body.at("stateRevision").get<std::uint64_t>();

        const auto snapshot = state();
        EXPECT_EQ(snapshot.at("stateRevision"), revision);
        const auto& instrument = snapshot.at("instrument");
        const auto& trace = instrument.at("traces").at(0);
        EXPECT_EQ(trace.at("id"), preset_.defaultTraceId.value());
        EXPECT_EQ(trace.at("windowId"), initialWindowId);
        const auto measurementId =
            trace.at("measurementId").get<std::uint64_t>();
        EXPECT_EQ(findMeasurement(instrument, measurementId).at("type"), type);
    }
}

TEST_F(WebApiTraceMeasurementTest, RejectsInvalidTypeAndTraceId) {
    const std::array<Json, 2> payloads{
        Json{{"traceId", preset_.defaultTraceId.value()},
             {"measurementType", "S33"}},
        Json{{"traceId", "not-an-id"}, {"measurementType", "S11"}},
    };

    for (std::size_t index = 0; index < payloads.size(); ++index) {
        const auto response = post(commandRequest(
            "invalid-" + std::to_string(index), 0, payloads[index]));
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::BadRequest_400);
        EXPECT_EQ(response->body, R"({"error":"invalidCommand"})");
    }
    EXPECT_EQ(state().at("stateRevision"), 0);
}

}  // namespace
}  // namespace vna::web_api
