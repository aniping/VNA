#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

const ControlSnapshot* controlSuccess(const ControlResult& result) {
    return std::get_if<ControlSnapshot>(&result.outcome);
}

const ApplicationError* controlError(const ControlResult& result) {
    return std::get_if<ApplicationError>(&result.outcome);
}

const ApplicationError* commandError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<ApplicationError>(error);
}

CommandEnvelope windowCommand(
    std::string commandId,
    std::string sessionId,
    CommandOrigin origin,
    std::optional<std::uint64_t> revision = std::nullopt) {
    return {
        .commandId = CommandId{std::move(commandId)},
        .sessionId = SessionId{std::move(sessionId)},
        .instrumentId = InstrumentId{"instrument-1"},
        .origin = origin,
        .expectedStateRevision = revision,
        .payload = CreateWindowCommand{},
    };
}

TEST(ControlAuthorityTest, AttachesOnlyOneSessionWhileRemainingLocal) {
    CommandBus commandBus{
        InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    EXPECT_EQ(commandBus.snapshot().control.mode, ControlMode::Local);

    const auto attached = commandBus.tryAttachScpiSession(
        SessionId{"session-1"}, [] {});
    std::optional<ControlMode> destructorMode;
    auto token = std::shared_ptr<void>{nullptr, [&](void*) {
        destructorMode = commandBus.snapshot().control.mode;
    }};
    const auto duplicate = commandBus.tryAttachScpiSession(
        SessionId{"session-1"}, [token = std::move(token)] {});

    ASSERT_NE(controlSuccess(attached), nullptr);
    EXPECT_EQ(controlSuccess(attached)->mode, ControlMode::Local);
    EXPECT_EQ(attached.stateRevision, 0U);
    ASSERT_NE(controlError(duplicate), nullptr);
    EXPECT_EQ(controlError(duplicate)->code, ApplicationErrorCode::ResourceBusy);
    EXPECT_EQ(duplicate.stateRevision, 0U);
    EXPECT_EQ(destructorMode, ControlMode::Local);
    EXPECT_EQ(commandBus.snapshot().control.mode, ControlMode::Local);
    EXPECT_NE(controlSuccess(commandBus.detachScpiSession(SessionId{"session-1"})), nullptr);
}

TEST(ControlAuthorityTest, ActivatesAndDetachesOnlyTheOwningSession) {
    CommandBus commandBus{
        InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    int revocations = 0;
    ASSERT_NE(controlSuccess(commandBus.tryAttachScpiSession(
        SessionId{"session-1"}, [&] { ++revocations; })), nullptr);

    const auto activated = commandBus.activateScpiControl(
        SessionId{"session-1"});
    const auto repeated = commandBus.activateScpiControl(
        SessionId{"session-1"});
    const auto denied = commandBus.detachScpiSession(
        SessionId{"session-2"});
    const auto detached = commandBus.detachScpiSession(
        SessionId{"session-1"});

    ASSERT_NE(controlSuccess(activated), nullptr);
    EXPECT_EQ(controlSuccess(activated)->mode, ControlMode::Remote);
    EXPECT_EQ(activated.stateRevision, 1U);
    EXPECT_EQ(repeated.stateRevision, 1U);
    ASSERT_NE(controlError(denied), nullptr);
    EXPECT_EQ(controlError(denied)->code, ApplicationErrorCode::ControlDenied);
    EXPECT_EQ(denied.stateRevision, 1U);
    ASSERT_NE(controlSuccess(detached), nullptr);
    EXPECT_EQ(controlSuccess(detached)->mode, ControlMode::Local);
    EXPECT_EQ(detached.stateRevision, 2U);
    EXPECT_EQ(revocations, 0);
}

TEST(ControlAuthorityTest, AuthorizesMutationsByModeAndOwningSession) {
    CommandBus commandBus{
        InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    ASSERT_NE(controlSuccess(commandBus.tryAttachScpiSession(
        SessionId{"session-1"}, [] {})), nullptr);
    const auto localWeb = commandBus.dispatch(windowCommand(
        "web-local", "web-session", CommandOrigin::Web, 0));
    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(localWeb.outcome));
    ASSERT_NE(controlSuccess(commandBus.activateScpiControl(
        SessionId{"session-1"})), nullptr);

    const auto ownerScpi = commandBus.dispatch(windowCommand(
        "scpi-owner", "session-1", CommandOrigin::Scpi));
    const auto remoteWeb = commandBus.dispatch(windowCommand(
        "web-remote", "web-session", CommandOrigin::Web, 3));
    const auto otherScpi = commandBus.dispatch(windowCommand(
        "scpi-other", "session-2", CommandOrigin::Scpi));

    ASSERT_TRUE(std::holds_alternative<CommandSuccess>(ownerScpi.outcome));
    EXPECT_EQ(ownerScpi.stateRevision, 3U);
    ASSERT_NE(commandError(remoteWeb), nullptr);
    EXPECT_EQ(commandError(remoteWeb)->code, ApplicationErrorCode::ControlDenied);
    ASSERT_NE(commandError(otherScpi), nullptr);
    EXPECT_EQ(commandErrorCode(*commandError(otherScpi)), CommandErrorCode::ControlDenied);
    EXPECT_EQ(commandBus.snapshot().stateRevision, 3U);
    EXPECT_EQ(commandBus.snapshot().display.windows.size(), 2U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 2U);
    EXPECT_NE(controlSuccess(commandBus.detachScpiSession(SessionId{"session-1"})), nullptr);
}

TEST(ControlAuthorityTest, TakeoverCallbackCanReenterAndConfirmDetach) {
    CommandBus commandBus{
        InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    int revocations = 0;
    std::optional<StateSnapshot> callbackSnapshot;
    std::optional<ControlResult> callbackDetach;
    ASSERT_NE(controlSuccess(commandBus.tryAttachScpiSession(
        SessionId{"session-1"},
        [&] {
            ++revocations;
            callbackSnapshot = commandBus.snapshot();
            callbackDetach = commandBus.detachScpiSession(
                SessionId{"session-1"});
        })), nullptr);
    ASSERT_NE(controlSuccess(commandBus.activateScpiControl(
        SessionId{"session-1"})), nullptr);

    const auto takeover = commandBus.takeLocalControl();

    ASSERT_NE(controlSuccess(takeover), nullptr);
    EXPECT_EQ(takeover.stateRevision, 2U);
    EXPECT_EQ(controlSuccess(takeover)->mode, ControlMode::Local);
    EXPECT_EQ(revocations, 1);
    ASSERT_TRUE(callbackSnapshot.has_value());
    EXPECT_EQ(callbackSnapshot->stateRevision, 2U);
    EXPECT_EQ(callbackSnapshot->control.mode, ControlMode::Local);
    ASSERT_TRUE(callbackDetach.has_value());
    EXPECT_EQ(callbackDetach->stateRevision, 2U);
    EXPECT_NE(controlSuccess(commandBus.tryAttachScpiSession(
        SessionId{"session-2"}, [] {})), nullptr);
    EXPECT_NE(controlSuccess(commandBus.detachScpiSession(SessionId{"session-2"})), nullptr);
}

TEST(ControlAuthorityTest, RevokerFailureStaysClosedUntilOwnerDetaches) {
    CommandBus commandBus{
        InstrumentId{"instrument-1"}, vna::test::stoppedSingleSweepHandler()};
    int revocations = 0;
    ASSERT_NE(controlSuccess(commandBus.tryAttachScpiSession(
        SessionId{"session-1"},
        [&] {
            ++revocations;
            throw std::runtime_error{"transport failure"};
        })), nullptr);
    ASSERT_NE(controlSuccess(commandBus.activateScpiControl(
        SessionId{"session-1"})), nullptr);

    const auto takeover = commandBus.takeLocalControl();
    const auto busy = commandBus.tryAttachScpiSession(
        SessionId{"session-2"}, [] {});
    const auto denied = commandBus.dispatch(windowCommand(
        "late-scpi", "session-1", CommandOrigin::Scpi));
    const auto localWeb = commandBus.dispatch(windowCommand(
        "local-web", "web-session", CommandOrigin::Web, 2));
    const auto repeated = commandBus.takeLocalControl();

    EXPECT_EQ(takeover.stateRevision, 2U);
    EXPECT_EQ(revocations, 1);
    ASSERT_NE(controlError(busy), nullptr);
    EXPECT_EQ(commandErrorCode(*controlError(busy)), CommandErrorCode::ResourceBusy);
    ASSERT_NE(commandError(denied), nullptr);
    EXPECT_EQ(commandError(denied)->code, ApplicationErrorCode::ControlDenied);
    EXPECT_TRUE(std::holds_alternative<CommandSuccess>(localWeb.outcome));
    EXPECT_EQ(localWeb.stateRevision, 3U);
    EXPECT_EQ(repeated.stateRevision, 3U);
    EXPECT_EQ(revocations, 1);
    EXPECT_EQ(commandBus.detachScpiSession(
        SessionId{"session-1"}).stateRevision, 3U);
    EXPECT_NE(controlSuccess(commandBus.tryAttachScpiSession(
        SessionId{"session-2"}, [] {})), nullptr);
    EXPECT_NE(controlSuccess(commandBus.detachScpiSession(SessionId{"session-2"})), nullptr);
}

}  // namespace
}  // namespace vna::application
