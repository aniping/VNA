#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/operation_manager.hpp>
#include <vna/application/trace_display_frame_query.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>
#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

using Json = nlohmann::json;

class WebApiOperationTest : public ::testing::Test {
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

    httplib::Result get(const std::string& path) const {
        return httplib::Client{"127.0.0.1", port_}.Get(path);
    }

    application::OperationSnapshot createOperation(std::string commandId) {
        return operations_.create(application::OperationSubmission{
            application::CommandId{std::move(commandId)},
            application::SessionId{"session-1"},
            7});
    }

    application::OperationManager operations_;
    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"},
        vna::test::stoppedSingleSweepHandler()};
    application::TraceDisplayFrameRepository repository_{1};
    application::TraceDisplayFrameQuery query_{commandBus_, repository_};
    WebApi webApi_{
        commandBus_, operations_, query_, display_model::TraceId{1}};
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiOperationTest, RejectsInvalidAndMissingOperationIds) {
    constexpr const char* invalidPaths[] = {
        "/api/v1/operations/",
        "/api/v1/operations/0",
        "/api/v1/operations/-1",
        "/api/v1/operations/not-a-number",
        "/api/v1/operations/18446744073709551616",
    };
    for (const auto* path : invalidPaths) {
        SCOPED_TRACE(path);
        const auto response = get(path);
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::BadRequest_400);
        EXPECT_EQ(response->body, R"({"error":"invalid-operation-id"})");
    }

    const auto missing = get("/api/v1/operations/99");
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, httplib::StatusCode::NotFound_404);
    EXPECT_EQ(missing->body, R"({"error":"operation-not-found"})");
    const auto multiSegment = get("/api/v1/operations/1/extra");
    ASSERT_TRUE(multiSegment);
    EXPECT_EQ(multiSegment->status, httplib::StatusCode::NotFound_404);
}

TEST_F(WebApiOperationTest, SerializesQueuedRunningAndSucceededStates) {
    const auto operation = createOperation("successful-operation");

    const auto queued = get("/api/v1/operations/1");
    ASSERT_TRUE(queued);
    ASSERT_EQ(queued->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(queued->get_header_value("Cache-Control"), "no-store");
    auto body = Json::parse(queued->body);
    EXPECT_EQ(body.at("operationId"), 1);
    EXPECT_EQ(body.at("status"), "Queued");
    EXPECT_EQ(body.at("submittedAtStateRevision"), 7);

    ASSERT_TRUE(std::holds_alternative<application::OperationSnapshot>(
        operations_.markRunning(operation.id)));
    const auto running = get("/api/v1/operations/1");
    ASSERT_TRUE(running);
    EXPECT_EQ(Json::parse(running->body).at("status"), "Running");

    ASSERT_TRUE(std::holds_alternative<application::OperationSnapshot>(
        operations_.complete(
            operation.id,
            application::OperationSucceeded{frames::FrameId{23}})));
    const auto succeeded = get("/api/v1/operations/1");
    ASSERT_TRUE(succeeded);
    body = Json::parse(succeeded->body);
    EXPECT_EQ(body.at("status"), "Succeeded");
    EXPECT_EQ(body.at("frameId"), 23);
}

TEST_F(WebApiOperationTest, SerializesStableFailureCategory) {
    struct FailureCase {
        application::SingleSweepFailureCode code;
        const char* category;
    };
    constexpr std::array cases{
        FailureCase{application::SingleSweepFailureCode::RawSweepFailed,
                    "raw-sweep-failed"},
        FailureCase{application::SingleSweepFailureCode::RawFrameRejected,
                    "raw-frame-rejected"},
        FailureCase{
            application::SingleSweepFailureCode::MeasurementSynthesisFailed,
            "measurement-synthesis-failed"},
        FailureCase{
            application::SingleSweepFailureCode::LogMagnitudeProjectionFailed,
            "log-magnitude-projection-failed"},
        FailureCase{
            application::SingleSweepFailureCode::FrequencyMaterializationFailed,
            "frequency-materialization-failed"},
        FailureCase{
            application::SingleSweepFailureCode::TraceDisplayPublishFailed,
            "trace-display-publish-failed"},
        FailureCase{application::SingleSweepFailureCode::UnexpectedFailure,
                    "unexpected-failure"},
    };
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto operation = createOperation(
            "failed-operation-" + std::to_string(index));
        ASSERT_TRUE(std::holds_alternative<application::OperationSnapshot>(
            operations_.markRunning(operation.id)));
        ASSERT_TRUE(std::holds_alternative<application::OperationSnapshot>(
            operations_.complete(
                operation.id,
                application::OperationFailed{application::OperationFailure{
                    .code = cases[index].code}})));
        const auto failed = get(
            "/api/v1/operations/" + std::to_string(operation.id.value()));
        ASSERT_TRUE(failed);
        ASSERT_EQ(failed->status, httplib::StatusCode::OK_200);
        const auto body = Json::parse(failed->body);
        EXPECT_EQ(body.at("status"), "Failed");
        EXPECT_EQ(body.at("failureCategory"), cases[index].category);
    }
}

TEST_F(WebApiOperationTest, SerializesCancellationLifecycle) {
    const auto operation = createOperation("canceled-operation");
    ASSERT_TRUE(std::holds_alternative<application::OperationSnapshot>(
        operations_.requestCancel(operation.id)));

    const auto requested = get("/api/v1/operations/1");
    ASSERT_TRUE(requested);
    EXPECT_EQ(Json::parse(requested->body).at("status"), "CancelRequested");

    ASSERT_TRUE(std::holds_alternative<application::OperationSnapshot>(
        operations_.complete(operation.id, application::OperationCanceled{})));
    const auto canceled = get("/api/v1/operations/1");
    ASSERT_TRUE(canceled);
    EXPECT_EQ(Json::parse(canceled->body).at("status"), "Canceled");
}

}  // namespace
}  // namespace vna::web_api
