#include "operation_http_handler.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

namespace vna::web_api::detail {
namespace {

using Json = nlohmann::json;

std::optional<application::OperationId> parseOperationId(
    std::string_view text) {
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value == 0) {
        return std::nullopt;
    }
    return application::OperationId{value};
}

const char* statusName(const application::OperationState& state) {
    return std::visit(
        [](const auto& value) -> const char* {
            using State = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<State, application::OperationQueued>) {
                return "Queued";
            } else if constexpr (
                std::is_same_v<State, application::OperationRunning>) {
                return "Running";
            } else if constexpr (std::is_same_v<
                                     State,
                                     application::OperationCancelRequested>) {
                return "CancelRequested";
            } else if constexpr (
                std::is_same_v<State, application::OperationSucceeded>) {
                return "Succeeded";
            } else if constexpr (
                std::is_same_v<State, application::OperationFailed>) {
                return "Failed";
            } else {
                static_assert(
                    std::is_same_v<State, application::OperationCanceled>);
                return "Canceled";
            }
        },
        state);
}

const char* failureCategory(application::SingleSweepFailureCode code) {
    switch (code) {
        case application::SingleSweepFailureCode::RawSweepFailed:
            return "raw-sweep-failed";
        case application::SingleSweepFailureCode::RawFrameRejected:
            return "raw-frame-rejected";
        case application::SingleSweepFailureCode::MeasurementSynthesisFailed:
            return "measurement-synthesis-failed";
        case application::SingleSweepFailureCode::LogMagnitudeProjectionFailed:
            return "log-magnitude-projection-failed";
        case application::SingleSweepFailureCode::FrequencyMaterializationFailed:
            return "frequency-materialization-failed";
        case application::SingleSweepFailureCode::TraceDisplayPublishFailed:
            return "trace-display-publish-failed";
        case application::SingleSweepFailureCode::UnexpectedFailure:
            return "unexpected-failure";
    }
    std::terminate();
}

Json operationJson(const application::OperationSnapshot& operation) {
    Json body{
        {"operationId", operation.id.value()},
        {"status", statusName(operation.state)},
        {"submittedAtStateRevision", operation.submittedAtStateRevision},
    };
    if (const auto* succeeded =
            std::get_if<application::OperationSucceeded>(&operation.state)) {
        body["frameId"] = succeeded->frameId.value();
    }
    if (const auto* failed =
            std::get_if<application::OperationFailed>(&operation.state)) {
        body["failureCategory"] = failureCategory(failed->error.code);
    }
    return body;
}

void setJsonError(
    httplib::Response& response,
    int status,
    const char* error) {
    response.status = status;
    response.set_content(Json{{"error", error}}.dump(), "application/json");
}

}  // namespace

void handleOperation(
    const application::OperationManager& operations,
    const httplib::Request& request,
    httplib::Response& response) {
    response.set_header("Cache-Control", "no-store");
    const auto operationId = parseOperationId(request.matches[1].str());
    if (!operationId) {
        setJsonError(
            response,
            httplib::StatusCode::BadRequest_400,
            "invalid-operation-id");
        return;
    }
    const auto result = operations.snapshot(*operationId);
    const auto* snapshot = std::get_if<application::OperationSnapshot>(&result);
    if (snapshot == nullptr) {
        const auto& error = std::get<application::OperationError>(result);
        setJsonError(
            response,
            error.code == application::OperationErrorCode::NotFound
                ? httplib::StatusCode::NotFound_404
                : httplib::StatusCode::InternalServerError_500,
            error.code == application::OperationErrorCode::NotFound
                ? "operation-not-found"
                : "internal-error");
        return;
    }
    response.set_content(operationJson(*snapshot).dump(), "application/json");
}

}  // namespace vna::web_api::detail
