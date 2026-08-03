#include "completion_fence_internal.hpp"

#include <algorithm>
#include <utility>

namespace vna::application::internal {

void removeSubscription(
    const std::shared_ptr<detail::FenceSubscriptionState>& subscription) {
    const auto coordinator = subscription->coordinator;
    const std::scoped_lock lock{coordinator->mutex};
    auto& subscriptions = coordinator->subscriptions;
    subscriptions.erase(
        std::remove_if(
            subscriptions.begin(),
            subscriptions.end(),
            [&](const auto& candidate) {
                const auto current = candidate.lock();
                return !current || current == subscription;
            }),
        subscriptions.end());
}

void cancelSubscription(
    const std::shared_ptr<detail::FenceSubscriptionState>& subscription) noexcept {
    if (!subscription) {
        return;
    }
    {
        std::unique_lock lock{subscription->mutex};
        if (subscription->phase == detail::DeliveryPhase::Pending ||
            subscription->phase == detail::DeliveryPhase::Claimed) {
            subscription->phase = detail::DeliveryPhase::Canceled;
        } else if (subscription->phase == detail::DeliveryPhase::Running &&
                   subscription->callbackThread != std::this_thread::get_id()) {
            subscription->condition.wait(lock, [&] {
                return subscription->phase != detail::DeliveryPhase::Running;
            });
        }
    }
    removeSubscription(subscription);
}

}  // namespace vna::application::internal

namespace vna::application {

CompletionFence::CompletionFence(
    std::shared_ptr<detail::CompletionFenceState> state)
    : state_(std::move(state)) {}

bool CompletionFence::active() const noexcept {
    if (!state_) {
        return false;
    }
    const std::scoped_lock lock{state_->coordinator->mutex};
    return !state_->coordinator->shutdown;
}

FenceSubscription::FenceSubscription(
    std::shared_ptr<detail::FenceSubscriptionState> state)
    : state_(std::move(state)) {}

FenceSubscription::FenceSubscription(FenceSubscription&& other) noexcept
    : state_(std::move(other.state_)) {}

FenceSubscription& FenceSubscription::operator=(
    FenceSubscription&& other) noexcept {
    if (this != &other) {
        cancel();
        state_ = std::move(other.state_);
    }
    return *this;
}

FenceSubscription::~FenceSubscription() {
    cancel();
}

void FenceSubscription::cancel() noexcept {
    internal::cancelSubscription(std::exchange(state_, {}));
}

bool FenceSubscription::active() const noexcept {
    if (!state_) {
        return false;
    }
    const std::scoped_lock lock{state_->mutex};
    return state_->phase == detail::DeliveryPhase::Pending ||
           state_->phase == detail::DeliveryPhase::Claimed ||
           state_->phase == detail::DeliveryPhase::Running;
}

}  // namespace vna::application
