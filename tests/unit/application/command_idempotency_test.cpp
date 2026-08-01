#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

CommandEnvelope command(
    std::string commandId,
    std::optional<std::uint64_t> revision,
    CommandPayload payload,
    std::string sessionId = "session-1",
    CommandOrigin origin = CommandOrigin::Web) {
    return {
        .commandId = CommandId{std::move(commandId)},
        .sessionId = SessionId{std::move(sessionId)},
        .instrumentId = InstrumentId{"instrument-1"},
        .origin = origin,
        .expectedStateRevision = revision,
        .payload = std::move(payload),
    };
}

CommandEnvelope windowCommand(
    std::string commandId,
    std::optional<std::uint64_t> revision = 0,
    std::string sessionId = "session-1",
    CommandOrigin origin = CommandOrigin::Web) {
    return command(
        std::move(commandId),
        revision,
        CreateWindowCommand{},
        std::move(sessionId),
        origin);
}

const CommandSuccess* success(const CommandResult& result) {
    return std::get_if<CommandSuccess>(&result.outcome);
}

const ApplicationError* applicationError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<ApplicationError>(error);
}

TEST(CommandIdempotencyTest, RejectsZeroCapacity) {
    EXPECT_THROW(
        static_cast<void>(vna::test::StoppedCommandBus{
            InstrumentId{"instrument-1"}, 0}),
        std::invalid_argument);
}

TEST(CommandIdempotencyTest, ReplaysFirstCompleteSuccessfulResult) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    const auto command = windowCommand("command-1");

    const auto first = commandBus.dispatch(command);
    const auto replay = commandBus.dispatch(command);

    ASSERT_NE(success(first), nullptr);
    ASSERT_NE(success(replay), nullptr);
    EXPECT_EQ(replay.stateRevision, first.stateRevision);
    EXPECT_EQ(success(replay)->value, success(first)->value);
    const auto state = commandBus.snapshot();
    EXPECT_EQ(state.stateRevision, 1U);
    EXPECT_EQ(state.display.windows.size(), 1U);
}

TEST(CommandIdempotencyTest, RejectsReuseWithoutReplacingFirstResult) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    const auto command = windowCommand("command-1");
    const auto first = commandBus.dispatch(command);

    const auto reused =
        commandBus.dispatch(windowCommand("command-1", 1));

    ASSERT_NE(applicationError(reused), nullptr);
    EXPECT_EQ(
        applicationError(reused)->code,
        ApplicationErrorCode::CommandIdReuse);
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(reused.outcome)),
        CommandErrorCode::CommandIdReuse);
    EXPECT_EQ(reused.stateRevision, 1U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 1U);

    const auto replay = commandBus.dispatch(command);
    ASSERT_NE(success(replay), nullptr);
    EXPECT_EQ(replay.stateRevision, first.stateRevision);
    EXPECT_EQ(commandBus.snapshot().display.windows.size(), 1U);
}

TEST(CommandIdempotencyTest, RejectsSameCommandKeyWhenOriginChanges) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    const auto webCommand = windowCommand("command-1");
    const auto first = commandBus.dispatch(webCommand);
    ASSERT_NE(
        success(commandBus.dispatch(windowCommand("advance", 1))),
        nullptr);

    const auto reused = commandBus.dispatch(windowCommand(
        "command-1", 0, "session-1", CommandOrigin::Scpi));

    ASSERT_NE(applicationError(reused), nullptr);
    EXPECT_EQ(
        applicationError(reused)->code,
        ApplicationErrorCode::CommandIdReuse);
    EXPECT_EQ(reused.stateRevision, 2U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 2U);
    EXPECT_EQ(commandBus.stats().idempotencyEvictions, 0U);
    const auto replay = commandBus.dispatch(webCommand);
    ASSERT_NE(success(replay), nullptr);
    EXPECT_EQ(replay.stateRevision, first.stateRevision);
    EXPECT_EQ(success(replay)->value, success(first)->value);
    EXPECT_EQ(commandBus.snapshot().display.windows.size(), 2U);
}

TEST(CommandIdempotencyTest, EvictsOldestWithoutRefreshingReplay) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}, 2};
    const auto first = windowCommand("command-1");
    ASSERT_NE(success(commandBus.dispatch(first)), nullptr);
    ASSERT_NE(
        success(commandBus.dispatch(windowCommand("command-2", 1))),
        nullptr);
    ASSERT_NE(success(commandBus.dispatch(first)), nullptr);
    ASSERT_NE(
        success(commandBus.dispatch(windowCommand("command-3", 2))),
        nullptr);

    const auto afterEviction = commandBus.stats();
    EXPECT_EQ(afterEviction.idempotencyEntries, 2U);
    EXPECT_EQ(afterEviction.idempotencyEvictions, 1U);

    const auto retried = commandBus.dispatch(first);
    ASSERT_NE(applicationError(retried), nullptr);
    EXPECT_EQ(
        applicationError(retried)->code,
        ApplicationErrorCode::StateRevisionConflict);
    EXPECT_EQ(retried.stateRevision, 3U);
    EXPECT_EQ(commandBus.stats().idempotencyEvictions, 2U);
}

TEST(CommandIdempotencyTest, SeparatesSessionsWithTheSameCommandId) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};

    const auto first = commandBus.dispatch(
        windowCommand("shared-command", 0, "session-1"));
    const auto second = commandBus.dispatch(
        windowCommand("shared-command", 1, "session-2"));

    ASSERT_NE(success(first), nullptr);
    ASSERT_NE(success(second), nullptr);
    EXPECT_EQ(second.stateRevision, 2U);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 2U);
    EXPECT_EQ(commandBus.snapshot().display.windows.size(), 2U);
}

TEST(CommandIdempotencyTest, RejectsWrongInstrumentBeforeCacheLookup) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    auto wrong = windowCommand("command-1");
    wrong.instrumentId = InstrumentId{"instrument-2"};

    const auto rejected = commandBus.dispatch(wrong);

    ASSERT_NE(applicationError(rejected), nullptr);
    EXPECT_EQ(
        applicationError(rejected)->code,
        ApplicationErrorCode::WrongInstrument);
    EXPECT_EQ(commandBus.stats().idempotencyEntries, 0U);
    ASSERT_NE(success(commandBus.dispatch(windowCommand("command-1"))), nullptr);
}

TEST(CommandIdempotencyTest, ReplaysDomainAndDisplayFailures) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    const auto domainCommand = command(
        "domain-error",
        0,
        CreateMeasurementCommand{
            domain::ChannelId{99}, domain::MeasurementType::S11});
    const auto displayCommand = command(
        "display-error",
        0,
        UpdateTraceFormatCommand{
            display_model::TraceId{99},
            display_model::TraceFormat::Phase});
    const auto domainFirst = commandBus.dispatch(domainCommand);
    const auto displayFirst = commandBus.dispatch(displayCommand);
    ASSERT_NE(success(commandBus.dispatch(windowCommand("advance"))), nullptr);

    const auto domainReplay = commandBus.dispatch(domainCommand);
    const auto displayReplay = commandBus.dispatch(displayCommand);

    EXPECT_EQ(domainReplay.stateRevision, domainFirst.stateRevision);
    EXPECT_EQ(displayReplay.stateRevision, displayFirst.stateRevision);
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(domainReplay.outcome)),
        CommandErrorCode::ChannelNotFound);
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(displayReplay.outcome)),
        CommandErrorCode::TraceNotFound);
}

TEST(CommandIdempotencyTest, ReplaysRevisionConflictAtOriginalRevision) {
    vna::test::StoppedCommandBus commandBus{
        InstrumentId{"instrument-1"}};
    const auto conflict = windowCommand("conflict", 9);
    const auto first = commandBus.dispatch(conflict);
    ASSERT_NE(success(commandBus.dispatch(windowCommand("advance"))), nullptr);

    const auto replay = commandBus.dispatch(conflict);

    ASSERT_NE(applicationError(replay), nullptr);
    EXPECT_EQ(
        applicationError(replay)->code,
        ApplicationErrorCode::StateRevisionConflict);
    EXPECT_EQ(replay.stateRevision, first.stateRevision);
    EXPECT_EQ(replay.stateRevision, 0U);
}

}  // namespace
}  // namespace vna::application
