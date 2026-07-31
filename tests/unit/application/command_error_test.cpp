#include <gtest/gtest.h>

#include <string>
#include <utility>

#include <vna/application/command_bus.hpp>

namespace vna::application {
namespace {

const domain::DomainError* domainError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<domain::DomainError>(error);
}

const display_model::DisplayError* displayError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr
        ? nullptr
        : std::get_if<display_model::DisplayError>(error);
}

CommandEnvelope traceCommand(std::string commandId, CommandPayload payload) {
    return {
        .commandId = CommandId{std::move(commandId)},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = 0,
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
            display_model::TraceId{99},
            display_model::TraceFormat::Phase}));

    ASSERT_NE(displayError(result), nullptr);
    EXPECT_EQ(
        displayError(result)->code,
        display_model::DisplayErrorCode::TraceNotFound);
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(result.outcome)),
        CommandErrorCode::TraceNotFound);
}

TEST(CommandErrorTest, ReportsMissingTraceWhenRemoving) {
    CommandBus commandBus{InstrumentId{"instrument-1"}};

    const auto result = commandBus.dispatch(traceCommand(
        "remove-missing-trace",
        RemoveTraceCommand{display_model::TraceId{99}}));

    ASSERT_NE(displayError(result), nullptr);
    EXPECT_EQ(
        displayError(result)->code,
        display_model::DisplayErrorCode::TraceNotFound);
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(result.outcome)),
        CommandErrorCode::TraceNotFound);
}

TEST(CommandErrorTest, ClassifiesScaleDisplayErrors) {
    const CommandError invalidScale = display_model::DisplayError{
        .code = display_model::DisplayErrorCode::InvalidScalePerDivision};
    const CommandError unsupportedFormat = display_model::DisplayError{
        .code =
            display_model::DisplayErrorCode::ScaleNotSupportedForFormat};

    EXPECT_EQ(
        commandErrorCode(invalidScale),
        CommandErrorCode::InvalidScalePerDivision);
    EXPECT_EQ(
        commandErrorCode(unsupportedFormat),
        CommandErrorCode::ScaleNotSupportedForFormat);
}

}  // namespace
}  // namespace vna::application
