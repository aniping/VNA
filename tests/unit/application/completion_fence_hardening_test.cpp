#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

#include <vna/application/operation_manager.hpp>

namespace vna::application {
namespace {

using namespace std::chrono_literals;

class TestSignal {
public:
    void notify() {
        {
            const std::scoped_lock lock{mutex_};
            signaled_ = true;
        }
        condition_.notify_all();
    }

    void wait() {
        std::unique_lock lock{mutex_};
        condition_.wait(lock, [&] { return signaled_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool signaled_{false};
};

OperationSnapshot createRunningOperation(OperationManager& manager) {
    const auto operation = manager.create(OperationSubmission{
        CommandId{"command-1"}, SessionId{"session-1"}, 7});
    EXPECT_TRUE(std::holds_alternative<OperationSnapshot>(
        manager.markRunning(operation.id)));
    return operation;
}

TEST(CompletionFenceHardeningTest, CancelingClaimedSubscriptionPreventsStart) {
    OperationManager manager;
    const auto operation = createRunningOperation(manager);
    auto firstFence = manager.captureFence(SessionId{"session-1"});
    auto secondFence = manager.captureFence(SessionId{"session-1"});
    TestSignal callbackStarted;
    TestSignal releaseCallback;
    std::atomic<int> runningCallback{0};
    std::atomic<int> callbackCount{0};
    const auto callback = [&](int identity) {
        runningCallback.store(identity);
        ++callbackCount;
        callbackStarted.notify();
        releaseCallback.wait();
    };
    auto first = manager.subscribe(
        std::move(firstFence), [&] { callback(1); });
    auto second = manager.subscribe(
        std::move(secondFence), [&] { callback(2); });
    bool completed = false;
    std::thread completer([&] {
        completed = std::holds_alternative<OperationSnapshot>(
            manager.complete(
                operation.id, OperationSucceeded{frames::FrameId{1}}));
    });

    callbackStarted.wait();
    if (runningCallback.load() == 1) {
        second.cancel();
    } else {
        first.cancel();
    }
    releaseCallback.notify();
    completer.join();

    EXPECT_TRUE(completed);
    EXPECT_EQ(callbackCount.load(), 1);
    EXPECT_FALSE(first.active());
    EXPECT_FALSE(second.active());
}

TEST(CompletionFenceHardeningTest, ExternalCancelWaitsForRunningCallback) {
    OperationManager manager;
    const auto operation = createRunningOperation(manager);
    auto fence = manager.captureFence(SessionId{"session-1"});
    TestSignal callbackStarted;
    TestSignal releaseCallback;
    TestSignal cancelStarted;
    std::atomic<bool> callbackFinished{false};
    auto subscription = manager.subscribe(std::move(fence), [&] {
        callbackStarted.notify();
        releaseCallback.wait();
        callbackFinished.store(true);
    });
    std::thread completer([&] {
        static_cast<void>(
            manager.complete(
                operation.id, OperationSucceeded{frames::FrameId{1}}));
    });
    callbackStarted.wait();
    std::promise<void> cancelReturned;
    auto cancelResult = cancelReturned.get_future();
    std::thread canceler([&] {
        cancelStarted.notify();
        subscription.cancel();
        cancelReturned.set_value();
    });

    cancelStarted.wait();
    EXPECT_EQ(cancelResult.wait_for(100ms), std::future_status::timeout);
    releaseCallback.notify();
    canceler.join();
    completer.join();

    EXPECT_TRUE(callbackFinished.load());
    EXPECT_EQ(cancelResult.wait_for(0ms), std::future_status::ready);
    EXPECT_FALSE(subscription.active());
}

TEST(CompletionFenceHardeningTest, CallbackCanReleaseItsOwnSubscription) {
    OperationManager manager;
    const auto canceledOperation = createRunningOperation(manager);
    auto canceledFence = manager.captureFence(SessionId{"session-1"});
    std::optional<FenceSubscription> canceledSubscription;
    bool cancelReturned = false;
    canceledSubscription.emplace(manager.subscribe(
        std::move(canceledFence), [&] {
            canceledSubscription->cancel();
            cancelReturned = true;
        }));

    static_cast<void>(manager.complete(
        canceledOperation.id, OperationSucceeded{frames::FrameId{1}}));
    EXPECT_TRUE(cancelReturned);
    EXPECT_FALSE(canceledSubscription->active());

    const auto destroyedOperation = createRunningOperation(manager);
    auto destroyedFence = manager.captureFence(SessionId{"session-1"});
    std::optional<FenceSubscription> destroyedSubscription;
    bool destroyReturned = false;
    destroyedSubscription.emplace(manager.subscribe(
        std::move(destroyedFence), [&] {
            destroyedSubscription.reset();
            destroyReturned = true;
        }));

    static_cast<void>(manager.complete(
        destroyedOperation.id, OperationSucceeded{frames::FrameId{1}}));
    EXPECT_TRUE(destroyReturned);
    EXPECT_FALSE(destroyedSubscription.has_value());
}

TEST(CompletionFenceHardeningTest, CallbackExceptionDoesNotEscapeComplete) {
    OperationManager manager;
    const auto operation = createRunningOperation(manager);
    auto fence = manager.captureFence(SessionId{"session-1"});
    auto subscription = manager.subscribe(
        std::move(fence), [] { throw std::runtime_error{"callback failed"}; });
    std::optional<OperationResult> result;

    EXPECT_NO_THROW(result.emplace(
        manager.complete(
            operation.id, OperationSucceeded{frames::FrameId{1}})));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<OperationSnapshot>(*result));
    EXPECT_FALSE(subscription.active());
}

TEST(CompletionFenceHardeningTest, HandlesBecomeInactiveAfterManagerDestruction) {
    std::optional<CompletionFence> fence;
    std::optional<FenceSubscription> subscription;
    int callbackCount = 0;
    {
        OperationManager manager;
        static_cast<void>(manager.create(OperationSubmission{
            CommandId{"pending"}, SessionId{"session-1"}, 7}));
        fence.emplace(manager.captureFence(SessionId{"session-1"}));
        auto subscribedFence =
            manager.captureFence(SessionId{"session-1"});
        subscription.emplace(manager.subscribe(
            std::move(subscribedFence), [&] { ++callbackCount; }));
    }

    EXPECT_FALSE(fence->active());
    EXPECT_FALSE(subscription->active());
    subscription->cancel();
    subscription.reset();
    fence.reset();
    EXPECT_EQ(callbackCount, 0);
}

}  // namespace
}  // namespace vna::application
