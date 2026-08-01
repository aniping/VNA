#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <vna/application/factory_preset.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

using namespace std::chrono_literals;

class WebApiWebSocketTest : public ::testing::Test {
protected:
    WebApiWebSocketTest()
        : traceId_(preset_.continuousTracePreset.trace.id),
          commandBus_(
              application::InstrumentId{"instrument-1"},
              std::move(preset_.commandBusState)),
          query_(commandBus_, repository_),
          webApi_(commandBus_, operations_, query_, traceId_) {}

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

    application::TraceDisplayFrame frame(std::uint64_t sequence) const {
        return {
            .frameId = frames::FrameId{sequence},
            .traceId = traceId_,
            .measurementId = preset_.continuousTracePreset.measurement.id,
            .measurementType = preset_.continuousTracePreset.measurement.type,
            .stateRevision = 0,
            .generation = 1,
            .sequenceNumber = sequence,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = {10'000'000.0, 10'100'000.0, 10'200'000.0},
            .samples = application::CartesianTraceDisplaySamples{
                .unit = application::TraceDisplayUnit::Decibel,
                .values = {-10.0, -11.0, -12.0}},
        };
    }

    void publish(std::uint64_t sequence) {
        ASSERT_TRUE(repository_.publish(frame(sequence)).hasValue());
    }

    void expectFrame(
        httplib::ws::WebSocketClient& client,
        std::uint64_t sequence) const {
        std::string message;
        ASSERT_EQ(client.read(message), httplib::ws::ReadResult::Text);
        const auto body = nlohmann::json::parse(message);
        EXPECT_EQ(body.at("frameId"), sequence);
        EXPECT_EQ(body.at("traceId"), traceId_.value());
        EXPECT_EQ(body.at("stateRevision"), 0U);
        EXPECT_EQ(body.at("sequenceNumber"), sequence);
        EXPECT_EQ(body.at("format"), "logMagnitude");
        EXPECT_EQ(body.at("valueUnit"), "dB");
        EXPECT_EQ(body.at("frequenciesHz").size(), 3U);
        EXPECT_EQ(body.at("values").at(1), -11.0);
    }

    std::unique_ptr<httplib::ws::WebSocketClient> makeClient() const {
        auto client = std::make_unique<httplib::ws::WebSocketClient>(
            "ws://127.0.0.1:" + std::to_string(port_) +
            "/api/v1/display-frames");
        client->set_read_timeout(2, 0);
        return client;
    }

    application::FactoryPreset preset_{application::makeFactoryPreset()};
    const display_model::TraceId traceId_;
    application::OperationManager operations_;
    vna::test::StoppedCommandBus commandBus_;
    application::TraceDisplayFrameRepository repository_{1};
    application::TraceDisplayFrameQuery query_;
    WebApi webApi_;
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiWebSocketTest, StopWakesAReaderWithoutWaitingForAFrame) {
    httplib::ws::WebSocketClient client{
        "ws://127.0.0.1:" + std::to_string(port_) +
        "/api/v1/display-frames"};
    client.set_read_timeout(1, 0);
    ASSERT_TRUE(client.connect());
    auto reading = std::async(std::launch::async, [&client] {
        std::string ignored;
        return client.read(ignored);
    });
    auto stopping = std::async(std::launch::async, [this] { webApi_.stop(); });

    const auto stopStatus = stopping.wait_for(2s);
    if (stopStatus != std::future_status::ready) { client.close(); }
    stopping.get();
    serverThread_.join();
    const auto readStatus = reading.wait_for(2s);
    if (readStatus != std::future_status::ready) { client.close(); }
    EXPECT_EQ(stopStatus, std::future_status::ready);
    EXPECT_EQ(readStatus, std::future_status::ready);
    EXPECT_EQ(reading.get(), httplib::ws::ReadResult::Fail);
}

TEST_F(WebApiWebSocketTest, PublishingWakesWaitingClientWithRestJsonShape) {
    auto client = makeClient();
    ASSERT_TRUE(client->connect());
    auto reading = std::async(std::launch::async, [&client] {
        std::string message;
        const auto result = client->read(message);
        return std::pair{result, std::move(message)};
    });

    publish(1);

    const auto readStatus = reading.wait_for(2s);
    if (readStatus != std::future_status::ready) {
        client->close();
    }
    const auto [result, message] = reading.get();
    ASSERT_EQ(readStatus, std::future_status::ready);
    ASSERT_EQ(result, httplib::ws::ReadResult::Text);
    const auto body = nlohmann::json::parse(message);
    EXPECT_EQ(body.at("sequenceNumber"), 1U);
    EXPECT_EQ(body.at("frequenciesHz").at(2), 10'200'000.0);
    EXPECT_EQ(body.at("values").at(0), -10.0);
    client->close();
}

TEST_F(WebApiWebSocketTest, SendsRetainedAndNewFramesInStrictOrder) {
    publish(1);
    auto client = makeClient();
    ASSERT_TRUE(client->connect());

    expectFrame(*client, 1);
    ASSERT_TRUE(client->send("ignored-client-message"));
    publish(2);
    expectFrame(*client, 2);
    publish(3);
    expectFrame(*client, 3);

    client->close();
}

TEST_F(WebApiWebSocketTest, ReconnectReceivesCurrentRetainedFrame) {
    publish(1);
    {
        auto first = makeClient();
        ASSERT_TRUE(first->connect());
        expectFrame(*first, 1);
        first->close();
    }
    publish(7);

    auto reconnected = makeClient();
    ASSERT_TRUE(reconnected->connect());
    expectFrame(*reconnected, 7);
    reconnected->close();
}

}  // namespace
}  // namespace vna::web_api
