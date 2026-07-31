#include <vna/application/operation_manager.hpp>
#include <algorithm>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
namespace vna::application::detail {
enum class DeliveryPhase { Pending, Claimed, Running, Finished, Canceled };

struct FenceSubscriptionState;
struct FenceCoordinator {
    std::mutex mutex;
    std::vector<std::weak_ptr<FenceSubscriptionState>> subscriptions;
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
    DeliveryPhase phase{DeliveryPhase::Pending};
    std::vector<std::uint64_t> outstandingIds;
    FenceCallback callback;
};

}  // namespace vna::application::detail
namespace vna::application {
namespace {

using detail::DeliveryPhase;
using detail::FenceCoordinator;
using detail::FenceSubscriptionState;

bool isTerminal(const OperationState& state) {
    return std::holds_alternative<OperationSucceeded>(state) ||
           std::holds_alternative<OperationFailed>(state) ||
           std::holds_alternative<OperationCanceled>(state);
}

void removeSubscription(
    const std::shared_ptr<FenceSubscriptionState>& subscription) {
    const auto coordinator = subscription->coordinator;
    const std::scoped_lock lock{coordinator->mutex};
    std::erase_if(coordinator->subscriptions, [&](const auto& candidate) {
        const auto current = candidate.lock();
        return !current || current == subscription;
    });
}

void cancelSubscription(
    const std::shared_ptr<FenceSubscriptionState>& subscription) noexcept {
    if (!subscription) {
        return;
    }
    {
        const std::scoped_lock lock{subscription->mutex};
        if (subscription->phase == DeliveryPhase::Pending ||
            subscription->phase == DeliveryPhase::Claimed) {
            subscription->phase = DeliveryPhase::Canceled;
        }
    }
    removeSubscription(subscription);
}

void deliver(const std::shared_ptr<FenceSubscriptionState>& subscription) {
    FenceCallback callback;
    {
        const std::scoped_lock lock{subscription->mutex};
        if (subscription->phase != DeliveryPhase::Claimed) {
            return;
        }
        subscription->phase = DeliveryPhase::Running;
        callback = std::move(subscription->callback);
    }
    callback();
    {
        const std::scoped_lock lock{subscription->mutex};
        subscription->phase = DeliveryPhase::Finished;
    }
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

CompletionFence::CompletionFence(
    std::shared_ptr<detail::CompletionFenceState> state)
    : state_(std::move(state)) {}

bool CompletionFence::active() const noexcept {
    return state_ != nullptr;
}

FenceSubscription::FenceSubscription(
    std::shared_ptr<FenceSubscriptionState> state)
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
    cancelSubscription(std::exchange(state_, {}));
}

bool FenceSubscription::active() const noexcept {
    if (!state_) {
        return false;
    }
    const std::scoped_lock lock{state_->mutex};
    return state_->phase == DeliveryPhase::Pending ||
           state_->phase == DeliveryPhase::Claimed ||
           state_->phase == DeliveryPhase::Running;
}

OperationManager::OperationManager()
    : fenceCoordinator_(std::make_shared<FenceCoordinator>()) {}

OperationManager::~OperationManager() = default;

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
        std::visit([&operation](auto&& terminal) {
            operation->second.state =
                std::forward<decltype(terminal)>(terminal);
        }, std::move(outcome));
        completed = operation->second;
        ready = claimTerminal(fenceCoordinator_, operationId.value());
    }
    for (const auto& subscription : ready) {
        deliver(subscription);
    }
    return *completed;
}

}  // namespace vna::application
