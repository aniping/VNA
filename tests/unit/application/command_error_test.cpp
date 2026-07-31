#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <utility>

#include <vna/application/command_bus.hpp>

namespace vna::application {
namespace {

const domain::DomainError* domainError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<domain::DomainError>(error);
}

CommandEnvelope traceCommand(std::string commandId, CommandPayload payload) {
    return {
        .commandId = CommandId{std::move(commandId)},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = 0,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = std::move(payload),
    };
}

TEST(CommandErrorTest, PreservesInvalidSweepDomainError) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};
    const CommandEnvelope command{
        .commandId = CommandId{"invalid-sweep"},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = 0,
        .timeout = std::chrono::seconds{5},
        .priority = CommandPriority::Normal,
        .payload = CreateChannelCommand{domain::SweepSettings{
            .startFrequencyHz = 2'000'000'000,
            .stopFrequencyHz = 1'000'000'000,
            .points = 201,
            .ifBandwidthHz = 1'000,
            .powerDbm = -10.0,
        }},
    };

    const auto result = commandBus.dispatch(command);

    ASSERT_NE(domainError(result), nullptr);
    EXPECT_EQ(
        domainError(result)->code,
        domain::DomainErrorCode::InvalidSweepSettings);
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(result.outcome)),
        CommandErrorCode::InvalidSweepSettings);
    EXPECT_EQ(result.stateRevision, 0U);
}

TEST(CommandErrorTest, ReportsMissingTraceWhenUpdatingFormat) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};

    const auto result = commandBus.dispatch(traceCommand(
        "update-missing-trace",
        UpdateTraceFormatCommand{
            domain::TraceId{99},
            domain::TraceFormat::Phase}));

    ASSERT_NE(domainError(result), nullptr);
    EXPECT_EQ(
        domainError(result)->code,
        domain::DomainErrorCode::TraceNotFound);
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(result.outcome)),
        CommandErrorCode::TraceNotFound);
}

TEST(CommandErrorTest, ReportsMissingTraceWhenRemoving) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};

    const auto result = commandBus.dispatch(traceCommand(
        "remove-missing-trace",
        RemoveTraceCommand{domain::TraceId{99}}));

    ASSERT_NE(domainError(result), nullptr);
    EXPECT_EQ(
        domainError(result)->code,
        domain::DomainErrorCode::TraceNotFound);
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(result.outcome)),
        CommandErrorCode::TraceNotFound);
}

}  // namespace
}  // namespace vna::application
