#include <gtest/gtest.h>

#include <httplib.h>

#include <chrono>
#include <future>
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
              vna::test::stoppedSingleSweepHandler(),
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

    application::FactoryPreset preset_{application::makeFactoryPreset()};
    const display_model::TraceId traceId_;
    application::OperationManager operations_;
    application::CommandBus commandBus_;
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

}  // namespace
}  // namespace vna::web_api
