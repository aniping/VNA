#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <vna/application/factory_preset.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

using Json = nlohmann::json;

struct FrameSpec {
    std::uint64_t traceId;
    std::uint64_t measurementId;
    domain::MeasurementType measurementType;
    display_model::TraceFormat format;
};

class WebApiWebSocketFrameSetTest : public ::testing::Test {
protected:
    WebApiWebSocketFrameSetTest()
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

    application::TraceDisplayFrame makeFrame(
        FrameSpec spec,
        const std::vector<double>& frequencies,
        application::TraceDisplaySamples samples) const {
        return {
            .frameId = frames::FrameId{71},
            .traceId = display_model::TraceId{spec.traceId},
            .measurementId = domain::MeasurementId{spec.measurementId},
            .measurementType = spec.measurementType,
            .stateRevision = 42,
            .generation = 1,
            .sequenceNumber = 9,
            .format = spec.format,
            .frequenciesHz = frequencies,
            .samples = std::move(samples),
        };
    }

    application::TraceDisplayFrameSet smallSet() const {
        const std::vector<double> frequencies{10.0, 20.0, 30.0};
        return {
            .generation = 1,
            .sequenceNumber = 9,
            .frames = {
                makeCartesian({1, 1, domain::MeasurementType::S11,
                               display_model::TraceFormat::LogMagnitude},
                              frequencies, {-1.0, -2.0, -3.0}),
                makeCartesian({2, 2, domain::MeasurementType::S12,
                               display_model::TraceFormat::Phase},
                              frequencies, {170.0, -179.0, 0.0}),
                makeSmith({3, 3, domain::MeasurementType::S21,
                           display_model::TraceFormat::Smith}, frequencies),
                makeCartesian({4, 4, domain::MeasurementType::S22,
                               display_model::TraceFormat::LogMagnitude},
                              frequencies, {-4.0, -5.0, -6.0}),
            },
        };
    }

    application::TraceDisplayFrame makeCartesian(
        FrameSpec spec,
        const std::vector<double>& frequencies,
        std::vector<double> values) const {
        const auto unit = spec.format == display_model::TraceFormat::Phase
            ? application::TraceDisplayUnit::Degree
            : application::TraceDisplayUnit::Decibel;
        return makeFrame(
            spec, frequencies,
            application::CartesianTraceDisplaySamples{
                .unit = unit, .values = std::move(values)});
    }

    application::TraceDisplayFrame makeSmith(
        FrameSpec spec,
        const std::vector<double>& frequencies) const {
        return makeFrame(
            spec, frequencies,
            application::ComplexTraceDisplaySamples{
                .unit = application::TraceDisplayUnit::Unitless,
                .values = {{0.1, 0.2}, {-0.3, 0.4}, {0.5, -0.6}}});
    }

    application::TraceDisplayFrameSet maximumSet() const {
        std::vector<double> frequencies(frames::kMaxSweepPoints);
        std::vector<double> scalar(frames::kMaxSweepPoints);
        std::vector<frames::ComplexSample> complex(frames::kMaxSweepPoints);
        auto frequency = std::numeric_limits<double>::max();
        for (std::size_t index = frames::kMaxSweepPoints; index-- > 0;) {
            frequencies[index] = frequency;
            scalar[index] = index % 2 == 0
                ? std::numeric_limits<double>::max()
                : std::numeric_limits<double>::lowest();
            complex[index] = {scalar[index], -scalar[index]};
            frequency = std::nextafter(frequency, 0.0);
        }
        auto result = smallSet();
        for (auto& frame : result.frames) {
            frame.frequenciesHz = frequencies;
            if (frame.format == display_model::TraceFormat::Smith) {
                std::get<application::ComplexTraceDisplaySamples>(
                    frame.samples).values = complex;
            } else {
                std::get<application::CartesianTraceDisplaySamples>(
                    frame.samples).values = scalar;
            }
        }
        return result;
    }

    void publish(application::TraceDisplayFrameSet set) {
        const auto result = repository_.publishFrameSet(std::move(set));
        ASSERT_TRUE(std::holds_alternative<
                    application::TraceDisplayFrameSetHandle>(result));
    }

    std::unique_ptr<httplib::ws::WebSocketClient> makeClient() const {
        auto client = std::make_unique<httplib::ws::WebSocketClient>(
            "ws://127.0.0.1:" + std::to_string(port_) +
            "/api/v1/display-frames");
        client->set_read_timeout(2, 0);
        return client;
    }

    std::string readMessage(httplib::ws::WebSocketClient& client) const {
        std::string message;
        EXPECT_EQ(client.read(message), httplib::ws::ReadResult::Text);
        return message;
    }

    application::FactoryPreset preset_{application::makeFactoryPreset()};
    application::OperationManager operations_;
    vna::test::StoppedCommandBus commandBus_;
    application::TraceDisplayFrameRepository repository_{4};
    application::TraceDisplayFrameQuery query_;
    WebApi webApi_;
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiWebSocketFrameSetTest, SendsFourSParametersAndThreeFormats) {
    publish(smallSet());
    auto client = makeClient();
    ASSERT_TRUE(client->connect());

    const auto body = Json::parse(readMessage(*client));
    EXPECT_EQ(body.at("generation"), 1U);
    EXPECT_EQ(body.at("sequenceNumber"), 9U);
    ASSERT_EQ(body.at("frames").size(), 4U);
    const std::vector<std::string> types{"S11", "S12", "S21", "S22"};
    const std::vector<std::string> formats{
        "logMagnitude", "phase", "smith", "logMagnitude"};
    const std::vector<std::string> units{"dB", "degree", "U", "dB"};
    for (std::size_t index = 0; index < body.at("frames").size(); ++index) {
        const auto& frame = body.at("frames").at(index);
        EXPECT_EQ(frame.at("frameId"), 71U);
        EXPECT_EQ(frame.at("traceId"), index + 1U);
        EXPECT_EQ(frame.at("measurementId"), index + 1U);
        EXPECT_EQ(frame.at("measurementType"), types[index]);
        EXPECT_EQ(frame.at("generation"), 1U);
        EXPECT_EQ(frame.at("stateRevision"), 42U);
        EXPECT_EQ(frame.at("sequenceNumber"), 9U);
        EXPECT_EQ(frame.at("format"), formats[index]);
        EXPECT_EQ(frame.at("valueUnit"), units[index]);
        EXPECT_EQ(frame.at("frequenciesHz"), Json::array({10.0, 20.0, 30.0}));
    }
    EXPECT_EQ(body.at("frames").at(1).at("values").at(1), -179.0);
    EXPECT_EQ(body.at("frames").at(2).at("values").at(0),
              Json::array({0.1, 0.2}));
    client->close();
}

TEST_F(WebApiWebSocketFrameSetTest, SendsMaximumMixedSetWithoutTruncation) {
    publish(maximumSet());
    auto client = makeClient();
    ASSERT_TRUE(client->connect());

    const auto message = readMessage(*client);
    EXPECT_GT(message.size(), 131'072U);
    EXPECT_LE(message.size(), 1'048'576U);
    const auto body = Json::parse(message);
    ASSERT_EQ(body.at("frames").size(), 4U);
    for (const auto& frame : body.at("frames")) {
        EXPECT_EQ(frame.at("frequenciesHz").size(), frames::kMaxSweepPoints);
        EXPECT_EQ(frame.at("values").size(), frames::kMaxSweepPoints);
    }
    EXPECT_EQ(body.at("frames").at(2).at("values").at(0).size(), 2U);
    client->close();
}

}  // namespace
}  // namespace vna::web_api
