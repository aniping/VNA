#pragma once

#include <condition_variable>
#include <thread>
#include <utility>
#include <vector>

#include <vna/application/operation_manager.hpp>

namespace vna::application::detail {

enum class DeliveryPhase { Pending, Claimed, Running, Finished, Canceled };

struct FenceSubscriptionState;

struct FenceCoordinator {
    std::mutex mutex;
    std::vector<std::weak_ptr<FenceSubscriptionState>> subscriptions;
    bool shutdown{false};
};

struct CompletionFenceState {
    std::shared_ptr<FenceCoordinator> coordinator;
    std::vector<std::uint64_t> capturedIds;
};

struct FenceSubscriptionState {
    FenceSubscriptionState(
        std::shared_ptr<FenceCoordinator> owner,
        std::vector<std::uint64_t> outstanding,
        FenceCallback completion)
        : coordinator(std::move(owner)),
          outstandingIds(std::move(outstanding)),
          callback(std::move(completion)) {}

    std::shared_ptr<FenceCoordinator> coordinator;
    std::mutex mutex;
    std::condition_variable condition;
    DeliveryPhase phase{DeliveryPhase::Pending};
    std::thread::id callbackThread;
    std::vector<std::uint64_t> outstandingIds;
    FenceCallback callback;
};

}  // namespace vna::application::detail

namespace vna::application::internal {

void removeSubscription(
    const std::shared_ptr<detail::FenceSubscriptionState>& subscription);
void cancelSubscription(
    const std::shared_ptr<detail::FenceSubscriptionState>& subscription) noexcept;

}  // namespace vna::application::internal
