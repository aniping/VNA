#include <vna/application/operation_manager.hpp>
#include "completion_fence_internal.hpp"
#include <algorithm>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
namespace vna::application {
namespace {

using detail::DeliveryPhase;
using detail::FenceCoordinator;
using detail::FenceSubscriptionState;
using internal::removeSubscription;

bool isTerminal(const OperationState& state) {
    return std::holds_alternative<OperationSucceeded>(state) ||
           std::holds_alternative<OperationFailed>(state) ||
           std::holds_alternative<OperationCanceled>(state);
}

void deliver(const std::shared_ptr<FenceSubscriptionState>& subscription) {
    FenceCallback callback;
    {
        const std::scoped_lock lock{subscription->mutex};
        if (subscription->phase != DeliveryPhase::Claimed) {
            return;
        }
        subscription->phase = DeliveryPhase::Running;
        subscription->callbackThread = std::this_thread::get_id();
        callback = std::move(subscription->callback);
    }
    try {
        callback();
    } catch (...) {
        // Callback failures do not change operation completion.
    }
    {
        const std::scoped_lock lock{subscription->mutex};
        subscription->phase = DeliveryPhase::Finished;
        subscription->callbackThread = {};
    }
    subscription->condition.notify_all();
    removeSubscription(subscription);
}

std::vector<std::shared_ptr<FenceSubscriptionState>> claimTerminal(
    const std::shared_ptr<FenceCoordinator>& coordinator,
    std::uint64_t operationId) {
    std::vector<std::shared_ptr<FenceSubscriptionState>> ready;
    const std::scoped_lock lock{coordinator->mutex};
    auto candidate = coordinator->subscriptions.begin();
    while (candidate != coordinator->subscriptions.end()) {
        const auto subscription = candidate->lock();
        bool remove = !subscription;
        if (subscription) {
            const std::scoped_lock stateLock{subscription->mutex};
            std::erase(subscription->outstandingIds, operationId);
            remove = subscription->phase != DeliveryPhase::Pending ||
                     subscription->outstandingIds.empty();
            if (subscription->phase == DeliveryPhase::Pending &&
                subscription->outstandingIds.empty()) {
                subscription->phase = DeliveryPhase::Claimed;
                ready.push_back(subscription);
            }
        }
        candidate = remove ? coordinator->subscriptions.erase(candidate)
                           : std::next(candidate);
    }
    return ready;
}

}  // namespace

OperationManager::OperationManager()
    : fenceCoordinator_(std::make_shared<FenceCoordinator>()) {}

OperationManager::~OperationManager() {
    std::vector<std::shared_ptr<FenceSubscriptionState>> subscriptions;
    {
        const std::scoped_lock lock{fenceCoordinator_->mutex};
        fenceCoordinator_->shutdown = true;
        for (const auto& candidate : fenceCoordinator_->subscriptions) {
            if (const auto subscription = candidate.lock()) {
                subscriptions.push_back(subscription);
            }
        }
        fenceCoordinator_->subscriptions.clear();
    }
    for (const auto& subscription : subscriptions) {
        {
            const std::scoped_lock lock{subscription->mutex};
            if (subscription->phase == DeliveryPhase::Pending ||
                subscription->phase == DeliveryPhase::Claimed) {
                subscription->phase = DeliveryPhase::Canceled;
            }
        }
        subscription->condition.notify_all();
    }
}

CompletionFence OperationManager::captureFence(const SessionId& sessionId) {
    std::vector<std::uint64_t> capturedIds;
    const std::scoped_lock lock{mutex_};
    for (const auto& [id, operation] : operations_) {
        if (operation.sessionId == sessionId && !isTerminal(operation.state)) {
            capturedIds.push_back(id);
        }
    }
    return CompletionFence{std::make_shared<detail::CompletionFenceState>(
        fenceCoordinator_, std::move(capturedIds))};
}

FenceSubscription OperationManager::subscribe(
    CompletionFence fence,
    FenceCallback callback) {
    auto capture = std::move(fence.state_);
    if (!callback || !capture ||
        capture->coordinator != fenceCoordinator_) {
        throw std::invalid_argument{"invalid completion fence subscription"};
    }
    std::vector<std::uint64_t> outstanding;
    std::unique_lock managerLock{mutex_};
    for (const auto id : capture->capturedIds) {
        const auto operation = operations_.find(id);
        if (operation != operations_.end() &&
            !isTerminal(operation->second.state)) {
            outstanding.push_back(id);
        }
    }
    auto state = std::make_shared<FenceSubscriptionState>(
        fenceCoordinator_, std::move(outstanding), std::move(callback));
    bool ready = false;
    {
        const std::scoped_lock coordinatorLock{fenceCoordinator_->mutex};
        ready = state->outstandingIds.empty();
        state->phase = ready ? DeliveryPhase::Claimed : DeliveryPhase::Pending;
        if (!ready) {
            fenceCoordinator_->subscriptions.push_back(state);
        }
    }
    managerLock.unlock();
    FenceSubscription subscription{state};
    if (ready) {
        deliver(state);
    }
    return subscription;
}

OperationResult OperationManager::complete(
    OperationId operationId,
    OperationTerminalOutcome outcome) {
    std::vector<std::shared_ptr<FenceSubscriptionState>> ready;
    std::optional<OperationSnapshot> completed;
    {
        const std::scoped_lock lock{mutex_};
        const auto operation = operations_.find(operationId.value());
        if (operation == operations_.end()) {
            return OperationError{.code = OperationErrorCode::NotFound};
        }
        const bool running =
            std::holds_alternative<OperationRunning>(operation->second.state);
        const bool cancelRequested = std::holds_alternative<
            OperationCancelRequested>(operation->second.state);
        const bool canceled = std::holds_alternative<OperationCanceled>(outcome);
        if ((!running && !cancelRequested) || (running && canceled)) {
            return OperationError{.code = OperationErrorCode::InvalidTransition};
        }
        // Copy every potentially allocating correlation field before changing
        // state. After mutation, fence claim/delivery no longer depends on
        // constructing the caller's return snapshot.
        completed.emplace(operation->second);
        std::visit([&operation, &completed](auto&& terminal) {
            completed->state = terminal;
            operation->second.state =
                std::forward<decltype(terminal)>(terminal);
        }, std::move(outcome));
        ready = claimTerminal(fenceCoordinator_, operationId.value());
    }
    for (const auto& subscription : ready) {
        deliver(subscription);
    }
    return *completed;
}

void OperationManager::abandonQueued(OperationId operationId) {
    std::vector<std::shared_ptr<FenceSubscriptionState>> ready;
    {
        const std::scoped_lock lock{mutex_};
        const auto operation = operations_.find(operationId.value());
        if (operation == operations_.end() ||
            !std::holds_alternative<OperationQueued>(
                operation->second.state)) {
            return;
        }
        operation->second.state = OperationCanceled{};
        ready = claimTerminal(fenceCoordinator_, operationId.value());
    }
    for (const auto& subscription : ready) {
        deliver(subscription);
    }
}

}  // namespace vna::application
