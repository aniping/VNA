#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <vna/application/factory_preset.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

class WebApiWebSocketLatestTest : public ::testing::Test {
protected:
    WebApiWebSocketLatestTest()
        : traceId_(preset_.defaultTraceId),
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

    application::TraceDisplayFrame largeFrame(std::uint64_t sequence) const {
        application::TraceDisplayFrame result{
            .frameId = frames::FrameId{sequence},
            .traceId = traceId_,
            .measurementId = domain::MeasurementId{1},
            .measurementType = domain::MeasurementType::S21,
            .stateRevision = 0,
            .generation = 1,
            .sequenceNumber = sequence,
            .format = display_model::TraceFormat::LogMagnitude,
            .samples = application::CartesianTraceDisplaySamples{
                .unit = application::TraceDisplayUnit::Decibel},
        };
        result.frequenciesHz.resize(frames::kMaxSweepPoints);
        auto& values = std::get<application::CartesianTraceDisplaySamples>(
            result.samples).values;
        values.resize(frames::kMaxSweepPoints);
        auto frequency = std::numeric_limits<double>::max();
        for (std::size_t index = frames::kMaxSweepPoints; index-- > 0;) {
            result.frequenciesHz[index] = frequency;
            values[index] = index % 2 == 0
                ? std::numeric_limits<double>::max()
                : std::numeric_limits<double>::lowest();
            frequency = std::nextafter(frequency, 0.0);
        }
        return result;
    }

    void publish(std::uint64_t sequence) {
        ASSERT_TRUE(repository_.publish(largeFrame(sequence)).hasValue());
    }

    std::unique_ptr<httplib::ws::WebSocketClient> makeClient(
        bool constrainReceiveBuffer = false) const {
        auto client = std::make_unique<httplib::ws::WebSocketClient>(
            "ws://127.0.0.1:" + std::to_string(port_) +
            "/api/v1/display-frames");
        client->set_read_timeout(2, 0);
        if (constrainReceiveBuffer) {
            client->set_socket_options([](socket_t socket) {
                const int bytes = 1024;
                static_cast<void>(::setsockopt(
                    socket, SOL_SOCKET, SO_RCVBUF,
                    reinterpret_cast<const char*>(&bytes), sizeof(bytes)));
            });
        }
        return client;
    }

    std::optional<std::uint64_t> readSequence(
        httplib::ws::WebSocketClient& client,
        std::size_t* messageBytes = nullptr) const {
        std::string message;
        if (client.read(message) != httplib::ws::ReadResult::Text) {
            return std::nullopt;
        }
        if (messageBytes != nullptr) {
            *messageBytes = message.size();
        }
        return nlohmann::json::parse(message).at("sequenceNumber")
            .get<std::uint64_t>();
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

TEST_F(WebApiWebSocketLatestTest, MaximumValidFrameStaysWithinWireLimit) {
    publish(1);
    auto client = makeClient();
    ASSERT_TRUE(client->connect());

    std::size_t messageBytes = 0;
    const auto sequence = readSequence(*client, &messageBytes);

    ASSERT_TRUE(sequence.has_value());
    EXPECT_EQ(*sequence, 1U);
    EXPECT_LE(messageBytes, 131'072U);
    client->close();
}

TEST_F(WebApiWebSocketLatestTest, SlowClientSkipsToLatestSequence) {
    constexpr std::uint64_t lastSequence = 64;
    auto client = makeClient(true);
    ASSERT_TRUE(client->connect());
    for (std::uint64_t sequence = 1; sequence <= lastSequence; ++sequence) {
        publish(sequence);
    }

    std::uint64_t previous = 0;
    bool skipped = false;
    while (previous < lastSequence) {
        const auto current = readSequence(*client);
        ASSERT_TRUE(current.has_value());
        ASSERT_GT(*current, previous);
        skipped = skipped || *current > previous + 1;
        previous = *current;
    }

    EXPECT_TRUE(skipped);
    EXPECT_EQ(previous, lastSequence);
    client->close();
}

}  // namespace
}  // namespace vna::web_api
