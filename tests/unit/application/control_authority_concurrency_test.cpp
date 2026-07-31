#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

bool controlSucceeded(const ControlResult& result) {
    return std::holds_alternative<ControlSnapshot>(result.outcome);
}

void expectControlSuccessAtRevision(
    const ControlResult& result, std::uint64_t revision) {
    EXPECT_TRUE(controlSucceeded(result));
    EXPECT_EQ(result.stateRevision, revision);
}

using Rejection = std::pair<ApplicationErrorCode, std::uint64_t>;

Rejection rejection(const ControlResult& result) {
    return {std::get<ApplicationError>(result.outcome).code, result.stateRevision};
}

Rejection rejection(const CommandResult& result) {
    const auto& error = std::get<CommandError>(result.outcome);
    return {std::get<ApplicationError>(error).code, result.stateRevision};
}

CommandEnvelope windowCommand(
    const char* commandId,
    const char* sessionId,
    CommandOrigin origin,
    std::optional<std::uint64_t> revision = std::nullopt,
    const char* instrumentId = "instrument-1") {
    return {
        .commandId = CommandId{commandId},
        .sessionId = SessionId{sessionId},
        .instrumentId = InstrumentId{instrumentId},
        .origin = origin,
        .expectedStateRevision = revision,
        .payload = CreateWindowCommand{},
    };
}

class StartGate {
public:
    void arriveAndWait() {
        std::unique_lock lock{mutex_};
        ++arrived_;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
    }

    void releaseWhenReady() {
        std::unique_lock lock{mutex_};
        condition_.wait(lock, [this] { return arrived_ == 2; });
        released_ = true;
        condition_.notify_all();
    }

private:
    int arrived_{0};
    bool released_{false};
    std::mutex mutex_;
    std::condition_variable condition_;
};

ControlResult attachAtGate(
    CommandBus& commandBus, StartGate& gate, const char* sessionId) {
    gate.arriveAndWait();
    return commandBus.tryAttachScpiSession(SessionId{sessionId}, [] {});
}

class BlockedTakeover {
    struct State {
        State()
            : entered(enteredPromise.get_future().share()),
              release(releasePromise.get_future().share()) {}

        std::promise<void> enteredPromise;
        std::shared_future<void> entered;
        std::promise<void> releasePromise;
        std::shared_future<void> release;
        std::atomic<int> calls{0};
    };

public:
    BlockedTakeover() : state_(std::make_shared<State>()) {}
    ~BlockedTakeover() { finish(); }

    ScpiSessionRevoker revoker() const {
        return [state = state_] {
            ++state->calls;
            state->enteredPromise.set_value();
            state->release.wait();
        };
    }

    void start(CommandBus& commandBus) {
        thread_ = std::thread{[this, &commandBus] {
            result_ = commandBus.takeLocalControl();
        }};
    }

    void waitUntilEntered() const { state_->entered.wait(); }

    void finish() {
        if (thread_.joinable()) {
            state_->releasePromise.set_value();
            thread_.join();
        }
    }

    [[nodiscard]] int calls() const { return state_->calls.load(); }
    [[nodiscard]] const std::optional<ControlResult>& result() const { return result_; }

private:
    std::shared_ptr<State> state_;
    std::thread thread_;
    std::optional<ControlResult> result_;
};

TEST(ControlAuthorityConcurrencyTest, RevokingRemainsClosedUntilOwnerDetach) {
    CommandBus commandBus{InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    BlockedTakeover takeover;
    const auto attached = commandBus.tryAttachScpiSession(SessionId{"owner"}, takeover.revoker());
    const auto remote = commandBus.activateScpiControl(SessionId{"owner"});
    ASSERT_TRUE(controlSucceeded(attached));
    ASSERT_TRUE(controlSucceeded(remote));
    takeover.start(commandBus);
    takeover.waitUntilEntered();
    const auto busy = commandBus.tryAttachScpiSession(SessionId{"next"}, [] {});
    const auto ownerActivate = commandBus.activateScpiControl(SessionId{"owner"});
    const auto otherActivate = commandBus.activateScpiControl(SessionId{"next"});
    const auto denied = commandBus.dispatch(windowCommand("late-scpi", "owner", CommandOrigin::Scpi));
    const auto revoking = commandBus.snapshot();
    const auto repeated = commandBus.takeLocalControl();
    const auto web = commandBus.dispatch(windowCommand("local-web", "web", CommandOrigin::Web, 2));
    const auto otherDetach = commandBus.detachScpiSession(SessionId{"next"});
    takeover.finish();
    const auto stillBusy = commandBus.tryAttachScpiSession(SessionId{"next"}, [] {});
    const auto ownerDetach = commandBus.detachScpiSession(SessionId{"owner"});
    const auto nextAttach = commandBus.tryAttachScpiSession(SessionId{"next"}, [] {});
    static_cast<void>(commandBus.detachScpiSession(SessionId{"next"}));
    const auto finalState = commandBus.snapshot();
    ASSERT_TRUE(takeover.result().has_value());
    expectControlSuccessAtRevision(*takeover.result(), 2U);
    EXPECT_EQ(attached.stateRevision, 0U);
    EXPECT_EQ(remote.stateRevision, 1U);
    EXPECT_EQ(takeover.calls(), 1);
    EXPECT_EQ(rejection(busy), (Rejection{ApplicationErrorCode::ResourceBusy, 2U}));
    EXPECT_EQ(rejection(ownerActivate), (Rejection{ApplicationErrorCode::ControlDenied, 2U}));
    EXPECT_EQ(rejection(otherActivate), (Rejection{ApplicationErrorCode::ControlDenied, 2U}));
    EXPECT_EQ(rejection(denied), (Rejection{ApplicationErrorCode::ControlDenied, 2U}));
    EXPECT_EQ(revoking.stateRevision, 2U);
    EXPECT_EQ(revoking.control.mode, ControlMode::Local);
    EXPECT_EQ(revoking.display.windows.size(), 0U);
    expectControlSuccessAtRevision(repeated, 2U);
    EXPECT_TRUE(std::holds_alternative<CommandSuccess>(web.outcome));
    EXPECT_EQ(web.stateRevision, 3U);
    EXPECT_EQ(rejection(otherDetach), (Rejection{ApplicationErrorCode::ControlDenied, 3U}));
    EXPECT_EQ(rejection(stillBusy), (Rejection{ApplicationErrorCode::ResourceBusy, 3U}));
    expectControlSuccessAtRevision(ownerDetach, 3U);
    expectControlSuccessAtRevision(nextAttach, 3U);
    EXPECT_EQ(finalState.stateRevision, 3U);
    EXPECT_EQ(finalState.control.mode, ControlMode::Local);
    EXPECT_EQ(finalState.display.windows.size(), 1U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 1U);
    EXPECT_EQ(commandBus.stats().idempotencyEvictions, 0U);
}

TEST(ControlAuthorityConcurrencyTest, DetachCompletesBeforeTakeoverWithoutRevoking) {
    CommandBus commandBus{InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    std::atomic<int> revocations{0};
    const auto attached = commandBus.tryAttachScpiSession(
        SessionId{"owner"}, [&] { ++revocations; });
    const auto remote = commandBus.activateScpiControl(SessionId{"owner"});
    ASSERT_TRUE(controlSucceeded(attached));
    ASSERT_TRUE(controlSucceeded(remote));

    std::promise<void> detachedPromise;
    auto detached = detachedPromise.get_future().share();
    std::optional<ControlResult> detachResult;
    std::optional<ControlResult> takeoverResult;
    std::thread detachThread{[&] {
        detachResult = commandBus.detachScpiSession(SessionId{"owner"});
        detachedPromise.set_value();
    }};
    std::thread takeoverThread{[&] {
        detached.wait();
        takeoverResult = commandBus.takeLocalControl();
    }};
    detachThread.join();
    takeoverThread.join();
    const auto snapshot = commandBus.snapshot();

    ASSERT_TRUE(detachResult.has_value());
    ASSERT_TRUE(takeoverResult.has_value());
    expectControlSuccessAtRevision(*detachResult, 2U);
    expectControlSuccessAtRevision(*takeoverResult, 2U);
    EXPECT_EQ(revocations.load(), 0);
    EXPECT_EQ(snapshot.stateRevision, 2U);
    EXPECT_EQ(snapshot.control.mode, ControlMode::Local);
    EXPECT_EQ(snapshot.display.windows.size(), 0U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 0U);
    EXPECT_EQ(commandBus.stats().idempotencyEvictions, 0U);
}

TEST(ControlAuthorityConcurrencyTest, ConcurrentAttachAdmitsExactlyOneSession) {
    CommandBus commandBus{InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    StartGate gate;
    std::optional<ControlResult> firstResult;
    std::optional<ControlResult> secondResult;
    std::thread first{[&] { firstResult = attachAtGate(commandBus, gate, "first"); }};
    std::thread second{[&] { secondResult = attachAtGate(commandBus, gate, "second"); }};
    gate.releaseWhenReady();
    first.join();
    second.join();

    ASSERT_TRUE(firstResult.has_value());
    ASSERT_TRUE(secondResult.has_value());
    const bool firstWon = controlSucceeded(*firstResult);
    ASSERT_NE(firstWon, controlSucceeded(*secondResult));
    const auto& accepted = firstWon ? *firstResult : *secondResult;
    const auto& rejected = firstWon ? *secondResult : *firstResult;
    EXPECT_EQ(accepted.stateRevision, 0U);
    EXPECT_EQ(rejection(rejected), (Rejection{ApplicationErrorCode::ResourceBusy, 0U}));
    const auto snapshot = commandBus.snapshot();
    EXPECT_EQ(snapshot.stateRevision, 0U);
    EXPECT_EQ(snapshot.control.mode, ControlMode::Local);
    EXPECT_EQ(snapshot.display.windows.size(), 0U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 0U);
    EXPECT_EQ(commandBus.stats().idempotencyEvictions, 0U);
    const auto detached = commandBus.detachScpiSession(
        SessionId{firstWon ? "first" : "second"});
    expectControlSuccessAtRevision(detached, 0U);
}

}  // namespace
}  // namespace vna::application
