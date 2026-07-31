#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

constexpr domain::SweepSettings validSweep() {
    return {
        .startFrequencyHz = 1'000'000,
        .stopFrequencyHz = 2'000'000,
        .points = 3,
        .ifBandwidthHz = 1'000,
        .powerDbm = -10.0,
    };
}

application::CommandEnvelope command(
    std::string id,
    std::uint64_t revision,
    application::CommandPayload payload) {
    return {
        .commandId = application::CommandId{std::move(id)},
        .sessionId = application::SessionId{"session-1"},
        .instrumentId = application::InstrumentId{"instrument-1"},
        .origin = application::CommandOrigin::Web,
        .expectedStateRevision = revision,
        .payload = std::move(payload),
    };
}

bool succeeded(const application::CommandResult& result) {
    return std::holds_alternative<application::CommandSuccess>(result.outcome);
}

class WebApiDisplayFrameTest : public ::testing::Test {
protected:
    void SetUp() override {
        createLogMagnitudeTrace();
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

    void createLogMagnitudeTrace() {
        ASSERT_TRUE(succeeded(commandBus_.dispatch(command(
            "channel", 0, application::CreateChannelCommand{validSweep()}))));
        ASSERT_TRUE(succeeded(commandBus_.dispatch(command(
            "measurement",
            1,
            application::CreateMeasurementCommand{
                domain::ChannelId{1}, domain::MeasurementType::S11}))));
        ASSERT_TRUE(succeeded(commandBus_.dispatch(command(
            "window", 2, application::CreateWindowCommand{}))));
        ASSERT_TRUE(succeeded(commandBus_.dispatch(command(
            "trace",
            3,
            application::CreateTraceCommand{
                display_model::WindowId{1},
                domain::MeasurementId{1},
                display_model::TraceFormat::LogMagnitude}))));
    }

    application::TraceDisplayFrame frame(std::uint32_t points) const {
        application::TraceDisplayFrame result{
            .frameId = frames::FrameId{1},
            .traceId = display_model::TraceId{1},
            .stateRevision = 4,
            .sequenceNumber = 1,
            .format = display_model::TraceFormat::LogMagnitude,
            .valueUnit = display_model::ScaleUnit::Decibel,
        };
        result.frequenciesHz.reserve(points);
        result.values.reserve(points);
        for (std::uint32_t index = 0; index < points; ++index) {
            result.frequenciesHz.push_back(1'000'000.0 + index);
            result.values.push_back(-static_cast<double>(index) / 10.0);
        }
        return result;
    }

    application::TraceDisplayFrame maximumTextFrame() const {
        auto result = frame(frames::kMaxSweepPoints);
        auto frequency = std::numeric_limits<double>::max();
        for (std::size_t index = result.frequenciesHz.size(); index-- > 0;) {
            result.frequenciesHz[index] = frequency;
            result.values[index] = index % 2 == 0
                ? std::numeric_limits<double>::max()
                : std::numeric_limits<double>::lowest();
            frequency = std::nextafter(frequency, 0.0);
        }
        return result;
    }

    httplib::Result get(const std::string& path) const {
        return httplib::Client{"127.0.0.1", port_}.Get(path);
    }

    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler()};
    application::TraceDisplayFrameRepository repository_{2};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{commandBus_, query_};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiDisplayFrameTest, ReturnsNoContentUntilFrameIsPublished) {
    const auto revisionBefore = commandBus_.snapshot().stateRevision;

    const auto missingTrace = get("/api/v1/traces/99/display-frame");
    const auto missingFrame = get("/api/v1/traces/1/display-frame");

    ASSERT_TRUE(missingTrace);
    ASSERT_TRUE(missingFrame);
    EXPECT_EQ(missingTrace->status, httplib::StatusCode::NoContent_204);
    EXPECT_EQ(missingFrame->status, httplib::StatusCode::NoContent_204);
    EXPECT_EQ(missingTrace->get_header_value("Cache-Control"), "no-store");
    EXPECT_EQ(missingFrame->get_header_value("Cache-Control"), "no-store");
    EXPECT_TRUE(missingTrace->body.empty());
    EXPECT_TRUE(missingFrame->body.empty());
    EXPECT_EQ(commandBus_.snapshot().stateRevision, revisionBefore);
}

TEST_F(WebApiDisplayFrameTest, ReturnsLatestCompleteFrameAsNoStoreJson) {
    const auto published = repository_.publish(frame(3));
    ASSERT_TRUE(published.hasValue());

    const auto response = get("/api/v1/traces/1/display-frame");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(response->get_header_value("Content-Type"), "application/json");
    EXPECT_EQ(response->get_header_value("Cache-Control"), "no-store");
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("frameId"), 1);
    EXPECT_EQ(body.at("traceId"), 1);
    EXPECT_EQ(body.at("stateRevision"), 4);
    EXPECT_EQ(body.at("sequenceNumber"), 1);
    EXPECT_EQ(body.at("format"), "logMagnitude");
    EXPECT_EQ(body.at("valueUnit"), "dB");
    EXPECT_EQ(
        body.at("frequenciesHz"),
        nlohmann::json({1'000'000.0, 1'000'001.0, 1'000'002.0}));
    EXPECT_EQ(body.at("values"), nlohmann::json({0.0, -0.1, -0.2}));
}

TEST_F(WebApiDisplayFrameTest, RejectsMalformedTraceIdentities) {
    constexpr const char* invalidPaths[] = {
        "/api/v1/traces//display-frame",
        "/api/v1/traces/0/display-frame",
        "/api/v1/traces/-1/display-frame",
        "/api/v1/traces/not-a-number/display-frame",
        "/api/v1/traces/18446744073709551616/display-frame",
    };

    for (const auto* path : invalidPaths) {
        SCOPED_TRACE(path);
        const auto response = get(path);
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::BadRequest_400);
        EXPECT_EQ(response->body, R"({"error":"invalid-trace-id"})");
    }
}

TEST_F(WebApiDisplayFrameTest, DoesNotClaimMultiSegmentTracePaths) {
    const auto response = get("/api/v1/traces/1/extra/display-frame");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::NotFound_404);
}

TEST_F(WebApiDisplayFrameTest, KeepsMaximumFrameResponseWithinContractSize) {
    const auto published = repository_.publish(maximumTextFrame());
    ASSERT_TRUE(published.hasValue());

    const auto response = get("/api/v1/traces/1/display-frame");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_LE(response->body.size(), 131'072U);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("values").size(), frames::kMaxSweepPoints);
}

}  // namespace
}  // namespace vna::web_api
