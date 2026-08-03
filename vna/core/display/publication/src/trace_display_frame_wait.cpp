#include <vna/application/trace_display_frame_repository.hpp>

#include <utility>

namespace vna::application {

TraceDisplayFrameHandle TraceDisplayFrameRepository::waitForNext(
    display_model::TraceId traceId,
    std::uint64_t afterSequence,
    vna::compat::StopToken token,
    TraceDisplayFrameWaitValidation validate) const {
    std::unique_lock lock{mutex_};
    if (token.stopRequested()) {
        return nullptr;
    }
    auto& entry = waitStates_[traceId.value()];
    if (entry == nullptr) {
        entry = std::make_shared<WaitState>();
    }
    const auto state = entry;
    const auto generation = state->discardGeneration;
    ++state->waiters;
    lock.unlock();
    auto registration =
        WaitRegistration{state, traceId.value(), afterSequence, generation};
    if (validate) {
        try {
            if (!validate()) {
                releaseWaitRegistration(registration);
                return nullptr;
            }
        } catch (...) {
            releaseWaitRegistration(registration);
            throw;
        }
    }
    return awaitRegistered(std::move(registration), token);
}

void TraceDisplayFrameRepository::releaseWaitRegistration(
    WaitRegistration registration) const {
    std::lock_guard lock{mutex_};
    --registration.state->waiters;
    cleanWaitState(registration.traceId, registration.state);
}

TraceDisplayFrameHandle TraceDisplayFrameRepository::awaitRegistered(
    WaitRegistration registration,
    vna::compat::StopToken token) const {
    TraceDisplayFrameHandle result;
    {
        // Cancellation changes stop state outside this repository. Taking the
        // same mutex before notification closes the predicate-to-sleep gap.
        vna::compat::StopCallback notify{token, [this, state = registration.state] {
            std::lock_guard guard{mutex_};
            state->changed.notify_all();
        }};
        std::unique_lock lock{mutex_};
        registration.state->changed.wait(lock, [&] {
            const auto latest = latestByTrace_.find(registration.traceId);
            return token.stopRequested() ||
                   registration.state->discardGeneration !=
                       registration.discardGeneration ||
                   (latest != latestByTrace_.end() &&
                    latest->second->sequenceNumber >
                        registration.afterSequence);
        });
        --registration.state->waiters;
        const auto discarded = registration.state->discardGeneration !=
                               registration.discardGeneration;
        const auto latest = latestByTrace_.find(registration.traceId);
        if (!token.stopRequested() && !discarded &&
            latest != latestByTrace_.end()) {
            result = latest->second;
        }
        cleanWaitState(registration.traceId, registration.state);
        // stop_callback destruction may wait for its callback. Releasing the
        // mutex first prevents a cancelling thread from deadlocking here.
        lock.unlock();
    }
    return result;
}

void TraceDisplayFrameRepository::cleanWaitState(
    std::uint64_t traceId,
    const std::shared_ptr<WaitState>& state) const {
    const auto retained = latestByTrace_.find(traceId) != latestByTrace_.end();
    const auto found = waitStates_.find(traceId);
    if (!retained && state->waiters == 0 && found != waitStates_.end() &&
        found->second == state) {
        waitStates_.erase(found);
    }
}

void TraceDisplayFrameRepository::discard(
    display_model::TraceId traceId) noexcept {
    std::shared_ptr<WaitState> waitState;
    {
        std::lock_guard lock{mutex_};
        latestByTrace_.erase(traceId.value());
        const auto waiting = waitStates_.find(traceId.value());
        if (waiting != waitStates_.end()) {
            waitState = waiting->second;
            ++waitState->discardGeneration;
            cleanWaitState(traceId.value(), waitState);
        }
    }
    if (waitState != nullptr) {
        waitState->changed.notify_all();
    }
}

}  // namespace vna::application
