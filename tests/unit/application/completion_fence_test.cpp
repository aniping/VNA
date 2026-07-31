#include <gtest/gtest.h>

#include <vna/application/operation_manager.hpp>

namespace vna::application {
namespace {

OperationSnapshot createOperation(
    OperationManager& manager,
    const char* commandId,
    const char* sessionId) {
    return manager.create(OperationSubmission{
        CommandId{commandId}, SessionId{sessionId}, 7});
}

void succeed(OperationManager& manager, OperationId operationId) {
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(operationId)));
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.complete(operationId, OperationSucceeded{})));
}

TEST(CompletionFenceTest, CapturesOnlyExistingOperationsForSession) {
    OperationManager manager;
    const auto captured =
        createOperation(manager, "captured", "session-1");
    const auto otherSession =
        createOperation(manager, "other-session", "session-2");
    auto fence = manager.captureFence(SessionId{"session-1"});
    const auto future = createOperation(manager, "future", "session-1");
    int callbackCount = 0;
    auto subscription = manager.subscribe(
        std::move(fence), [&callbackCount] { ++callbackCount; });

    succeed(manager, otherSession.id);
    succeed(manager, future.id);
    EXPECT_EQ(callbackCount, 0);
    EXPECT_TRUE(subscription.active());

    succeed(manager, captured.id);
    EXPECT_EQ(callbackCount, 1);
    EXPECT_FALSE(subscription.active());
}

TEST(CompletionFenceTest, InvokesSatisfiedFenceInsideSubscribe) {
    OperationManager manager;
    bool emptyCalled = false;
    auto emptyFence = manager.captureFence(SessionId{"session-1"});

    auto emptySubscription = manager.subscribe(
        std::move(emptyFence), [&emptyCalled] { emptyCalled = true; });

    EXPECT_TRUE(emptyCalled);
    EXPECT_FALSE(emptySubscription.active());

    const auto operation = createOperation(manager, "captured", "session-1");
    auto completedFence = manager.captureFence(SessionId{"session-1"});
    succeed(manager, operation.id);
    bool completedCalled = false;
    bool completedStateVisible = false;

    auto completedSubscription = manager.subscribe(std::move(completedFence), [&] {
        completedCalled = true;
        const auto snapshot = manager.snapshot(operation.id);
        completedStateVisible = std::holds_alternative<OperationSucceeded>(
            std::get<OperationSnapshot>(snapshot).state);
    });

    EXPECT_TRUE(completedCalled);
    EXPECT_TRUE(completedStateVisible);
    EXPECT_FALSE(completedSubscription.active());
}

TEST(CompletionFenceTest, EveryTerminalOutcomeSatisfiesOutsideManagerLock) {
    OperationManager manager;
    const auto succeeded = createOperation(manager, "success", "session-1");
    const auto failed = createOperation(manager, "failure", "session-1");
    const auto canceled = createOperation(manager, "cancel", "session-1");
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(succeeded.id)));
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(failed.id)));
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.requestCancel(canceled.id)));
    auto fence = manager.captureFence(SessionId{"session-1"});
    int callbackCount = 0;
    bool sawCanceledState = false;
    auto subscription = manager.subscribe(std::move(fence), [&] {
        ++callbackCount;
        const auto snapshot = manager.snapshot(canceled.id);
        sawCanceledState = std::holds_alternative<OperationCanceled>(
            std::get<OperationSnapshot>(snapshot).state);
    });

    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.complete(succeeded.id, OperationSucceeded{})));
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(manager.complete(
        failed.id,
        OperationFailed{OperationFailure{
            .code = SingleSweepFailureCode::RawSweepFailed}})));
    EXPECT_EQ(callbackCount, 0);
    ASSERT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.complete(canceled.id, OperationCanceled{})));

    EXPECT_EQ(callbackCount, 1);
    EXPECT_TRUE(sawCanceledState);
    EXPECT_FALSE(subscription.active());
}

TEST(CompletionFenceTest, PendingCancelPreventsCallback) {
    OperationManager manager;
    const auto operation = createOperation(manager, "captured", "session-1");
    auto fence = manager.captureFence(SessionId{"session-1"});
    int callbackCount = 0;
    auto subscription = manager.subscribe(
        std::move(fence), [&callbackCount] { ++callbackCount; });
    ASSERT_TRUE(subscription.active());

    subscription.cancel();
    subscription.cancel();
    succeed(manager, operation.id);

    EXPECT_FALSE(subscription.active());
    EXPECT_EQ(callbackCount, 0);
}

TEST(CompletionFenceTest, MovesHandlesAndRejectsInvalidFences) {
    OperationManager manager;
    static_cast<void>(createOperation(manager, "pending", "session-1"));
    auto originalFence = manager.captureFence(SessionId{"session-1"});
    auto movedFence = std::move(originalFence);
    EXPECT_FALSE(originalFence.active());
    EXPECT_TRUE(movedFence.active());
    auto assignedFence = manager.captureFence(SessionId{"session-1"});
    assignedFence = std::move(movedFence);
    EXPECT_FALSE(movedFence.active());
    EXPECT_TRUE(assignedFence.active());
    OperationManager foreignManager;
    EXPECT_THROW(foreignManager.subscribe(
        std::move(assignedFence), [] {}), std::invalid_argument);

    auto emptyCallbackFence = manager.captureFence(SessionId{"session-1"});
    EXPECT_THROW(manager.subscribe(
        std::move(emptyCallbackFence), {}), std::invalid_argument);
    EXPECT_THROW(manager.subscribe(
        std::move(emptyCallbackFence), [] {}), std::invalid_argument);

    auto original = manager.subscribe(
        manager.captureFence(SessionId{"session-1"}), [] {});
    auto moved = std::move(original);
    EXPECT_FALSE(original.active());
    EXPECT_TRUE(moved.active());
    auto assigned = manager.subscribe(
        manager.captureFence(SessionId{"session-1"}), [] {});
    assigned = std::move(moved);
    EXPECT_FALSE(moved.active());
    EXPECT_TRUE(assigned.active());
}

}  // namespace
}  // namespace vna::application
