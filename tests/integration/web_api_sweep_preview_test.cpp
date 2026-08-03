#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;
struct PreviewTraceSpec {
    std::uint64_t id;
    domain::MeasurementType type;
    display_model::TraceFormat format;
};

class WebApiSweepPreviewTest : public ::testing::Test {
protected:
    WebApiSweepPreviewTest()
        : commandBus_(
              application::InstrumentId{"instrument-1"},
              std::move(preset_.commandBusState)),
          query_(commandBus_, repository_),
          webApi_(
              commandBus_, operations_, query_,
              {repository_, commandBus_.previews()}) {}

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
    application::SweepPreview preview(std::size_t points) const {
        const std::vector<double> frequencies{10.0, 20.0, 30.0};
        return {
            .identity = {1, acquisition::SweepId{7}},
            .channelId = domain::ChannelId{1},
            .stateRevision = 4,
            .sequenceNumber = 7,
            .totalPointCount = 3,
            .traces = {
                cartesianTrace({1, domain::MeasurementType::S11,
                                display_model::TraceFormat::LogMagnitude},
                               frequencies, {-1.0, -2.0, -3.0}, points),
                cartesianTrace({2, domain::MeasurementType::S12,
                                display_model::TraceFormat::Phase},
                               frequencies, {10.0, 20.0, 30.0}, points),
                smithTrace(frequencies, points),
            },
        };
    }
    application::SweepTracePreview cartesianTrace(
        PreviewTraceSpec spec,
        std::vector<double> frequencies,
        std::vector<double> values,
        std::size_t points) const {
        frequencies.resize(points);
        values.resize(points);
        const auto unit = spec.format == display_model::TraceFormat::Phase
            ? application::TraceDisplayUnit::Degree
            : application::TraceDisplayUnit::Decibel;
        return {
            display_model::TraceId{spec.id}, domain::MeasurementId{spec.id},
            spec.type, spec.format, std::move(frequencies),
            application::CartesianTraceDisplaySamples{unit, std::move(values)}};
    }
    application::SweepTracePreview smithTrace(
        std::vector<double> frequencies,
        std::size_t points) const {
        std::vector<frames::ComplexSample> values{
            {0.1, 0.2}, {0.3, -0.4}, {-0.5, 0.6}};
        frequencies.resize(points);
        values.resize(points);
        return {
            display_model::TraceId{3}, domain::MeasurementId{3},
            domain::MeasurementType::S21, display_model::TraceFormat::Smith,
            std::move(frequencies),
            application::ComplexTraceDisplaySamples{
                application::TraceDisplayUnit::Unitless, std::move(values)}};
    }
    void publish(std::size_t points) {
        const auto result = commandBus_.previews().publish(preview(points));
        ASSERT_TRUE(std::holds_alternative<
                    application::SweepPreviewHandle>(result));
    }
    std::unique_ptr<httplib::ws::WebSocketClient> connect() const {
        auto client = std::make_unique<httplib::ws::WebSocketClient>(
            "ws://127.0.0.1:" + std::to_string(port_) +
            "/api/v1/sweep-previews");
        client->set_read_timeout(2, 0);
        if (!client->connect()) {
            return nullptr;
        }
        return client;
    }
    Json read(httplib::ws::WebSocketClient& client) const {
        std::string message;
        EXPECT_EQ(client.read(message), httplib::ws::ReadResult::Text);
        return Json::parse(message);
    }
    void expectAvailable(
        const Json& body,
        std::uint64_t eventCursor,
        std::size_t points) const {
        EXPECT_EQ(body.at("type"), "available");
        EXPECT_EQ(body.at("eventCursor"), eventCursor);
        EXPECT_FALSE(body.contains("cursor"));
        EXPECT_EQ(body.at("generation"), 1U);
        EXPECT_EQ(body.at("sweepId"), 7U);
        EXPECT_EQ(body.at("channelId"), 1U);
        EXPECT_EQ(body.at("stateRevision"), 4U);
        EXPECT_EQ(body.at("sequenceNumber"), 7U);
        EXPECT_EQ(body.at("totalPointCount"), 3U);
        EXPECT_EQ(body.at("sweepStatus").at("generation"), 1U);
        EXPECT_EQ(body.at("sweepStatus").at("activePreviewIdentity"),
                  Json({{"generation", 1}, {"sweepId", 7}}));
        ASSERT_EQ(body.at("traces").size(), 3U);
        EXPECT_EQ(body.at("traces").at(0).at("traceId"), 1U);
        EXPECT_EQ(body.at("traces").at(0).at("measurementId"), 1U);
        EXPECT_EQ(body.at("traces").at(0).at("measurementType"), "S11");
        EXPECT_EQ(body.at("traces").at(0).at("format"), "logMagnitude");
        EXPECT_EQ(body.at("traces").at(0).at("frequenciesHz").size(), points);
        EXPECT_EQ(body.at("traces").at(0).at("valueUnit"), "dB");
        EXPECT_EQ(body.at("traces").at(1).at("valueUnit"), "degree");
        EXPECT_EQ(body.at("traces").at(2).at("valueUnit"), "U");
        EXPECT_EQ(body.at("traces").at(0).at("values").size(), points);
        EXPECT_EQ(body.at("traces").at(2).at("values").at(0),
                  Json::array({0.1, 0.2}));
    }

    application::FactoryPreset preset_{vna::test::singleSweepFactoryPreset()};
    application::OperationManager operations_;
    vna::test::StoppedCommandBus commandBus_;
    application::TraceDisplayFrameRepository repository_{4};
    application::TraceDisplayFrameQuery query_;
    WebApi webApi_;
    int port_{-1};
    std::thread serverThread_;
};
TEST_F(WebApiSweepPreviewTest, PublishesAvailablePrefixToWaitingClient) {
    auto client = connect();
    ASSERT_NE(client, nullptr);
    EXPECT_EQ(read(*client).at("type"), "status");
    auto reading = std::async(std::launch::async, [&client] {
        std::string message;
        const auto result = client->read(message);
        return std::pair{result, std::move(message)};
    });

    publish(2);

    const auto status = reading.wait_for(2s);
    if (status != std::future_status::ready) {
        client->close();
    }
    ASSERT_EQ(status, std::future_status::ready);
    const auto [result, message] = reading.get();
    ASSERT_EQ(result, httplib::ws::ReadResult::Text);
    expectAvailable(Json::parse(message), 2, 2);
    client->close();
}

TEST_F(WebApiSweepPreviewTest, ReconnectBootstrapsLatestCumulativePrefix) {
    publish(2);
    auto first = connect();
    ASSERT_NE(first, nullptr);
    expectAvailable(read(*first), 2, 2);
    first->close();

    publish(3);
    auto reconnected = connect();
    ASSERT_NE(reconnected, nullptr);
    expectAvailable(read(*reconnected), 3, 3);
    reconnected->close();
}

TEST_F(WebApiSweepPreviewTest, StreamsInvalidationAndGenerationAdvance) {
    publish(2);
    auto client = connect();
    ASSERT_NE(client, nullptr);
    expectAvailable(read(*client), 2, 2);

    EXPECT_TRUE(commandBus_.previews().invalidate({1, acquisition::SweepId{7}}));
    const auto invalidated = read(*client);
    EXPECT_EQ(invalidated.at("type"), "invalidated");
    EXPECT_EQ(invalidated.at("eventCursor"), 3U);
    EXPECT_EQ(invalidated.at("generation"), 1U);
    EXPECT_EQ(invalidated.at("sweepStatus").at("activePreviewIdentity"),
              nullptr);
    const auto advanced = commandBus_.previews().advanceGeneration(2);
    ASSERT_TRUE(std::holds_alternative<
                application::SweepPreviewGenerationAdvanced>(advanced));
    const auto generation = read(*client);
    EXPECT_EQ(generation.at("type"), "generationAdvanced");
    EXPECT_EQ(generation.at("eventCursor"), 4U);
    EXPECT_EQ(generation.at("generation"), 2U);
    EXPECT_TRUE(generation.at("sweepStatus")
                    .at("firstSweepAfterConfiguration"));
    client->close();
}

TEST_F(WebApiSweepPreviewTest, StopWakesReaderWithoutAnEvent) {
    auto client = connect();
    ASSERT_NE(client, nullptr);
    EXPECT_EQ(read(*client).at("type"), "status");
    auto reading = std::async(std::launch::async, [&client] {
        std::string message;
        return client->read(message);
    });
    auto stopping = std::async(std::launch::async, [this] { webApi_.stop(); });

    const auto status = stopping.wait_for(2s);
    if (status != std::future_status::ready) { client->close(); }
    stopping.get();
    if (serverThread_.joinable()) { serverThread_.join(); }
    EXPECT_EQ(status, std::future_status::ready);
    const auto readStatus = reading.wait_for(2s);
    if (readStatus != std::future_status::ready) { client->close(); }
    EXPECT_EQ(readStatus, std::future_status::ready);
}

}  // namespace
}  // namespace vna::web_api
