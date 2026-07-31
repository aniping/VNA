#include <gtest/gtest.h>

#include <vna/application/operation_manager.hpp>

namespace vna::application {
namespace {

OperationSnapshot createOperation(OperationManager& manager) {
    return manager.create(OperationSubmission{
        CommandId{"command-1"}, SessionId{"session-1"}, 7});
}

OperationErrorCode errorCode(const OperationResult& result) {
    return std::get<OperationError>(result).code;
}

TEST(OperationManagerTest, CreatesQueuedOperationWithSubmissionContext) {
    OperationManager manager;

    const auto snapshot = manager.create(OperationSubmission{
        .commandId = CommandId{"command-1"},
        .sessionId = SessionId{"session-1"},
        .submittedAtStateRevision = 7,
    });

    EXPECT_EQ(snapshot.id, OperationId{1});
    EXPECT_EQ(snapshot.commandId, CommandId{"command-1"});
    EXPECT_EQ(snapshot.sessionId, SessionId{"session-1"});
    EXPECT_EQ(snapshot.submittedAtStateRevision, 7U);
    EXPECT_TRUE(std::holds_alternative<OperationQueued>(snapshot.state));
}

TEST(OperationManagerTest, RetrievesCreatedOperationById) {
    OperationManager manager;
    const auto created = createOperation(manager);

    const auto result = manager.snapshot(created.id);

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->id, created.id);
    EXPECT_TRUE(std::holds_alternative<OperationQueued>(snapshot->state));
}

TEST(OperationManagerTest, MarksQueuedOperationRunning) {
    OperationManager manager;
    const auto created = createOperation(manager);

    const auto result = manager.markRunning(created.id);

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(std::holds_alternative<OperationRunning>(snapshot->state));
    EXPECT_TRUE(std::holds_alternative<OperationRunning>(
        std::get<OperationSnapshot>(manager.snapshot(created.id)).state));
}

TEST(OperationManagerTest, RepeatingMarkRunningIsIdempotent) {
    OperationManager manager;
    const auto created = createOperation(manager);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(created.id)));

    const auto result = manager.markRunning(created.id);

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(std::holds_alternative<OperationRunning>(snapshot->state));
}

TEST(OperationManagerTest, RequestsCancellationWithoutCancelingImmediately) {
    OperationManager manager;
    const auto created = createOperation(manager);

    const auto result = manager.requestCancel(created.id);

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(
        std::holds_alternative<OperationCancelRequested>(snapshot->state));
    EXPECT_FALSE(std::holds_alternative<OperationCanceled>(snapshot->state));
}

TEST(OperationManagerTest, RequestsCancellationOfRunningOperation) {
    OperationManager manager;
    const auto created = createOperation(manager);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(created.id)));

    const auto result = manager.requestCancel(created.id);

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(
        std::holds_alternative<OperationCancelRequested>(snapshot->state));
}

TEST(OperationManagerTest, RepeatingCancelRequestIsIdempotent) {
    OperationManager manager;
    const auto created = createOperation(manager);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.requestCancel(created.id)));

    const auto result = manager.requestCancel(created.id);

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(
        std::holds_alternative<OperationCancelRequested>(snapshot->state));
}

TEST(OperationManagerTest, CompletesRunningOperationSuccessfully) {
    OperationManager manager;
    const auto created = createOperation(manager);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(created.id)));

    const auto result = manager.complete(created.id, OperationSucceeded{});

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->commandId, CommandId{"command-1"});
    EXPECT_EQ(snapshot->sessionId, SessionId{"session-1"});
    EXPECT_EQ(snapshot->submittedAtStateRevision, 7U);
    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(snapshot->state));
    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(
        std::get<OperationSnapshot>(manager.snapshot(created.id)).state));
}

TEST(OperationManagerTest, RetainsStructuredFailureInTerminalState) {
    OperationManager manager;
    const auto created = createOperation(manager);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(created.id)));

    const auto result = manager.complete(
        created.id,
        OperationFailed{OperationFailure{
            .code = SingleSweepFailureCode::MeasurementSynthesisFailed}});

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    const auto* failed = std::get_if<OperationFailed>(&snapshot->state);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(
        failed->error.code,
        SingleSweepFailureCode::MeasurementSynthesisFailed);
}

TEST(OperationManagerTest, BackendCompletesCancelRequestAsCanceled) {
    OperationManager manager;
    const auto created = createOperation(manager);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.requestCancel(created.id)));

    const auto result = manager.complete(created.id, OperationCanceled{});

    const auto* snapshot = std::get_if<OperationSnapshot>(&result);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(std::holds_alternative<OperationCanceled>(snapshot->state));
}

TEST(OperationManagerTest, GeneratesOperationIdsInsideManager) {
    OperationManager manager;

    const auto first = createOperation(manager);
    const auto second = createOperation(manager);

    EXPECT_EQ(first.id, OperationId{1});
    EXPECT_EQ(second.id, OperationId{2});
}

TEST(OperationManagerTest, ReportsNotFoundThroughEveryLookupOperation) {
    OperationManager manager;
    const OperationId missing{99};

    EXPECT_EQ(errorCode(manager.snapshot(missing)), OperationErrorCode::NotFound);
    EXPECT_EQ(
        errorCode(manager.markRunning(missing)), OperationErrorCode::NotFound);
    EXPECT_EQ(
        errorCode(manager.requestCancel(missing)), OperationErrorCode::NotFound);
    EXPECT_EQ(
        errorCode(manager.complete(missing, OperationSucceeded{})),
        OperationErrorCode::NotFound);
}

TEST(OperationManagerTest, AcceptsTerminalRaceAfterCancelRequest) {
    OperationManager manager;
    const auto succeeded = createOperation(manager);
    const auto failed = createOperation(manager);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.requestCancel(succeeded.id)));
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.requestCancel(failed.id)));

    const auto successResult =
        manager.complete(succeeded.id, OperationSucceeded{});
    const auto failureResult = manager.complete(
        failed.id,
        OperationFailed{OperationFailure{
            .code = SingleSweepFailureCode::RawSweepFailed}});

    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(
        std::get<OperationSnapshot>(successResult).state));
    EXPECT_TRUE(std::holds_alternative<OperationFailed>(
        std::get<OperationSnapshot>(failureResult).state));
}

TEST(OperationManagerTest, RejectsIllegalTransitionsAndPreservesTerminalState) {
    OperationManager manager;
    const auto created = createOperation(manager);

    EXPECT_EQ(
        errorCode(manager.complete(created.id, OperationSucceeded{})),
        OperationErrorCode::InvalidTransition);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(created.id)));
    EXPECT_EQ(
        errorCode(manager.complete(created.id, OperationCanceled{})),
        OperationErrorCode::InvalidTransition);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.requestCancel(created.id)));
    EXPECT_EQ(
        errorCode(manager.markRunning(created.id)),
        OperationErrorCode::InvalidTransition);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.complete(created.id, OperationCanceled{})));

    EXPECT_EQ(
        errorCode(manager.requestCancel(created.id)),
        OperationErrorCode::InvalidTransition);
    EXPECT_EQ(
        errorCode(manager.complete(created.id, OperationSucceeded{})),
        OperationErrorCode::InvalidTransition);
    EXPECT_TRUE(std::holds_alternative<OperationCanceled>(
        std::get<OperationSnapshot>(manager.snapshot(created.id)).state));
}

}  // namespace
}  // namespace vna::application
