#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {
using Json = nlohmann::json;
using namespace std::chrono_literals;

application::StateSnapshot presetState(
    const application::FactoryPreset& preset) {
    return {0, {}, preset.commandBusState.instrument.snapshot(),
            preset.commandBusState.displayWorkspace.snapshot()};
}

application::SweepRuntimePlan runtimePlan(
    application::TracePublicationCatalog& catalog) {
    auto acquisition = acquisition::test_support::validPlan();
    acquisition.minimumSweepPeriod = 1h;
    return {std::move(acquisition), catalog.capture(), 2};
}

acquisition::RawSweepCaptureResult completeSweep(
    const acquisition::RawSweepCaptureRequest& request,
    const acquisition::RawSweepChunkObserver&,
    std::stop_token) {
    return acquisition::test_support::validPayload(request.sequenceNumber);
}

class WebApiSweepStatusTest : public ::testing::Test {
protected:
    WebApiSweepStatusTest()
        : catalog_(preset_.acquisitionChannelId, repository_,
                   presetState(preset_)),
          previews_(application::initialSweepRuntimeStatus(runtimePlan(catalog_))),
          runtime_(runtimePlan(catalog_), completeSweep, previews_, catalog_,
                   operations_),
          commandBus_(application::InstrumentId{"instrument-1"}, runtime_,
                      std::move(preset_.commandBusState)),
          query_(commandBus_, repository_),
          webApi_(commandBus_, operations_, query_, {repository_, previews_}) {}

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

    std::unique_ptr<httplib::ws::WebSocketClient> connect() const {
        auto client = std::make_unique<httplib::ws::WebSocketClient>(
            "ws://127.0.0.1:" + std::to_string(port_) +
            "/api/v1/sweep-previews");
        client->set_read_timeout(2, 0);
        return client->connect() ? std::move(client) : nullptr;
    }

    application::FactoryPreset preset_{application::makeFactoryPreset()};
    application::OperationManager operations_;
    application::TraceDisplayFrameRepository repository_{4};
    application::TracePublicationCatalog catalog_;
    application::SweepPreviewExchange previews_;
    application::SweepRuntime runtime_;
    application::CommandBus commandBus_;
    application::TraceDisplayFrameQuery query_;
    WebApi webApi_;
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiSweepStatusTest, ReturnsToPreparingAfterContinuousSweep) {
    ASSERT_TRUE(repository_.waitForNextSet({1, 0}).has_value());
    const auto completed = previews_.waitForNext({3});
    ASSERT_TRUE(completed.has_value());
    ASSERT_EQ(std::visit([](const auto& event) {
        return event.status.runtime.userPhase;
    }, *completed), application::SweepUserPhase::Preparing);
    auto client = connect();
    ASSERT_NE(client, nullptr);
    std::string message;
    ASSERT_EQ(client->read(message), httplib::ws::ReadResult::Text);
    const auto body = Json::parse(message);

    EXPECT_EQ(body.at("type"), "status");
    EXPECT_EQ(body.at("eventCursor"), 4U);
    const auto& status = body.at("sweepStatus");
    EXPECT_EQ(status.at("generation"), 1U);
    EXPECT_EQ(status.at("channelId"), 1U);
    EXPECT_EQ(status.at("stateRevision"), 0U);
    EXPECT_EQ(status.at("sweepId"), nullptr);
    EXPECT_EQ(status.at("userPhase"), "preparing");
    EXPECT_EQ(status.at("progress").at("completedAcquisitionPoints"), 0U);
    EXPECT_EQ(status.at("progress").at("totalAcquisitionPoints"), 6U);
    EXPECT_FALSE(status.at("firstSweepAfterConfiguration"));
    EXPECT_EQ(status.at("activePreviewIdentity"), nullptr);
    client->close();
    httplib::Client http{"127.0.0.1", port_};
    const auto response = http.Get("/api/v1/state");
    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(Json::parse(response->body)
                  .at("sweepRuntime").at("phase"), "preparing");
    EXPECT_EQ(response->body.find("publishing"), std::string::npos);
}

}  // namespace
}  // namespace vna::web_api
