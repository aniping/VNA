#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

bool succeeded(const application::CommandResult& result) {
    return std::holds_alternative<application::CommandSuccess>(result.outcome);
}

class WebApiWebSocketLifecycleTest : public ::testing::Test {
protected:
    WebApiWebSocketLifecycleTest()
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

    application::TraceDisplayFrame frame(std::uint64_t sequence) const {
        return {
            .frameId = frames::FrameId{sequence},
            .traceId = traceId_,
            .stateRevision = 0,
            .sequenceNumber = sequence,
            .format = display_model::TraceFormat::LogMagnitude,
            .valueUnit = display_model::ScaleUnit::Decibel,
            .frequenciesHz = {10'000'000.0, 10'100'000.0},
            .values = {-10.0, -11.0},
        };
    }

    void publish(std::uint64_t sequence) {
        ASSERT_TRUE(repository_.publish(frame(sequence)).hasValue());
    }

    std::unique_ptr<httplib::ws::WebSocketClient> connect() const {
        auto client = std::make_unique<httplib::ws::WebSocketClient>(
            "ws://127.0.0.1:" + std::to_string(port_) +
            "/api/v1/display-frames");
        client->set_read_timeout(2, 0);
        if (!client->connect()) {
            return {};
        }
        return client;
    }

    void expectSequence(
        httplib::ws::WebSocketClient& client,
        std::uint64_t expected) const {
        std::string message;
        ASSERT_EQ(client.read(message), httplib::ws::ReadResult::Text);
        EXPECT_EQ(
            nlohmann::json::parse(message).at("sequenceNumber"), expected);
    }

    application::CommandResult dispatch(application::CommandPayload payload) {
        return commandBus_.dispatch(application::CommandEnvelope{
            .commandId = application::CommandId{"invalidate-trace"},
            .sessionId = application::SessionId{"session-1"},
            .instrumentId = application::InstrumentId{"instrument-1"},
            .origin = application::CommandOrigin::Web,
            .expectedStateRevision = 0,
            .payload = std::move(payload),
        });
    }

    void expectInvalidationCloses(application::CommandPayload payload) {
        publish(1);
        auto client = connect();
        ASSERT_NE(client, nullptr);
        expectSequence(*client, 1);

        const auto result = dispatch(std::move(payload));
        ASSERT_TRUE(succeeded(result));
        EXPECT_EQ(result.stateRevision, 1U);
        publish(2);

        std::string ignored;
        EXPECT_EQ(client->read(ignored), httplib::ws::ReadResult::Fail);
        ASSERT_NE(repository_.latest(traceId_), nullptr);
        EXPECT_EQ(repository_.latest(traceId_)->sequenceNumber, 2U);
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

TEST_F(WebApiWebSocketLifecycleTest, SequentialDisconnectsReuseCapacity) {
    publish(1);
    for (std::size_t index = 0; index < 40; ++index) {
        auto client = connect();
        ASSERT_NE(client, nullptr) << "connection " << index;
        expectSequence(*client, 1);
        client->close();
    }

    publish(2);
    auto current = connect();
    ASSERT_NE(current, nullptr);
    expectSequence(*current, 2);
    current->close();
    const auto health = httplib::Client{"127.0.0.1", port_}.Get("/api/v1/health");
    ASSERT_TRUE(health);
    EXPECT_EQ(health->status, httplib::StatusCode::OK_200);
}

TEST_F(WebApiWebSocketLifecycleTest, RemovingTraceClosesActiveStream) {
    expectInvalidationCloses(
        application::RemoveTraceCommand{.traceId = traceId_});
}

TEST_F(WebApiWebSocketLifecycleTest, ChangingFormatClosesActiveStream) {
    expectInvalidationCloses(application::UpdateTraceFormatCommand{
        .traceId = traceId_,
        .format = display_model::TraceFormat::Phase});
}

}  // namespace
}  // namespace vna::web_api
