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
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/single_sweep_command_handler.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

using namespace std::chrono_literals;

bool succeeded(const application::CommandResult& result) {
    return std::holds_alternative<application::CommandSuccess>(result.outcome);
}

application::StateSnapshot initialSnapshot(
    const application::FactoryPreset& preset) {
    return {
        .stateRevision = 0,
        .control = {},
        .instrument = preset.commandBusState.instrument.snapshot(),
        .display = preset.commandBusState.displayWorkspace.snapshot(),
    };
}

class WebApiWebSocketLifecycleTest
    : public ::testing::Test,
      private application::SingleSweepExecution {
protected:
    WebApiWebSocketLifecycleTest()
        : traceId_(preset_.defaultTraceId),
          catalog_(
              preset_.acquisitionChannelId,
              repository_,
              initialSnapshot(preset_)),
          sweepHandler_(*this),
          commandBus_(
              application::InstrumentId{"instrument-1"},
              sweepHandler_,
              catalog_,
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
            .measurementId = domain::MeasurementId{1},
            .measurementType = domain::MeasurementType::S21,
            .stateRevision = 0,
            .generation = 1,
            .sequenceNumber = sequence,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = {10'000'000.0, 10'100'000.0},
            .samples = application::CartesianTraceDisplaySamples{
                .unit = application::TraceDisplayUnit::Decibel,
                .values = {-10.0, -11.0}},
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

    application::CommandResult dispatch(
        application::CommandPayload payload,
        std::uint64_t revision = 0,
        std::string commandId = "invalidate-trace") {
        return commandBus_.dispatch(application::CommandEnvelope{
            .commandId = application::CommandId{std::move(commandId)},
            .sessionId = application::SessionId{"session-1"},
            .instrumentId = application::InstrumentId{"instrument-1"},
            .origin = application::CommandOrigin::Web,
            .expectedStateRevision = revision,
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

private:
    application::SingleSweepSubmitResult submit(
        application::SingleSweepWorkItem) override {
        return application::SingleSweepSubmitError{
            .code = application::SingleSweepSubmitErrorCode::Stopped};
    }

    void invalidateTraceFrame(
        display_model::TraceId traceId) noexcept override {
        repository_.discard(traceId);
    }

    void discardTrace(display_model::TraceId traceId) noexcept override {
        repository_.discard(traceId);
    }

protected:
    application::FactoryPreset preset_{application::makeFactoryPreset()};
    const display_model::TraceId traceId_;
    application::OperationManager operations_;
    application::TraceDisplayFrameRepository repository_{1};
    application::TracePublicationCatalog catalog_;
    application::SingleSweepCommandHandler sweepHandler_;
    application::CommandBus commandBus_;
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
    publish(1);
    auto client = connect();
    ASSERT_NE(client, nullptr);
    expectSequence(*client, 1);
    client->set_read_timeout(5, 0);
    auto reading = std::async(std::launch::async, [&client] {
        std::string ignored;
        return client->read(ignored);
    });

    const auto changed = dispatch(application::UpdateTraceFormatCommand{
        .traceId = traceId_,
        .format = display_model::TraceFormat::Phase});
    const auto readStatus = reading.wait_for(2s);
    if (readStatus != std::future_status::ready) {
        client->close();
    }
    const auto readResult = reading.get();
    ASSERT_TRUE(succeeded(changed));
    ASSERT_EQ(readStatus, std::future_status::ready);
    EXPECT_EQ(readResult, httplib::ws::ReadResult::Fail);
    EXPECT_EQ(repository_.latest(traceId_), nullptr);

    const auto restored = dispatch(
        application::UpdateTraceFormatCommand{
            .traceId = traceId_,
            .format = display_model::TraceFormat::LogMagnitude},
        1,
        "restore-trace-format");
    ASSERT_TRUE(succeeded(restored));
    publish(2);
    auto reconnected = connect();
    ASSERT_NE(reconnected, nullptr);
    expectSequence(*reconnected, 2);
    reconnected->close();
}

}  // namespace
}  // namespace vna::web_api
