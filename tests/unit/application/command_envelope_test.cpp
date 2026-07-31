#include <gtest/gtest.h>

#include <vna/application/command_bus.hpp>

namespace vna::application {
namespace {

template <typename T>
concept ExposesTimeout = requires(T value) { value.timeout; };

template <typename T>
concept ExposesPriority = requires(T value) { value.priority; };

static_assert(!ExposesTimeout<CommandEnvelope>);
static_assert(!ExposesPriority<CommandEnvelope>);

TEST(CommandEnvelopeTest, ContainsOnlyEffectiveCommandContext) {
    const CommandEnvelope command{
        .commandId = CommandId{"command-1"},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .expectedStateRevision = 7,
        .payload = CreateWindowCommand{},
    };

    EXPECT_EQ(command.commandId.value(), "command-1");
    EXPECT_EQ(command.sessionId.value(), "session-1");
    EXPECT_EQ(command.instrumentId.value(), "instrument-1");
    EXPECT_EQ(command.expectedStateRevision, 7U);
    EXPECT_TRUE(std::holds_alternative<CreateWindowCommand>(command.payload));
}

}  // namespace
}  // namespace vna::application
