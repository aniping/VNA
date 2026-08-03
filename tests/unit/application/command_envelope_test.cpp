#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

#include <vna/application/command_contract.hpp>

namespace vna::application {
namespace {

template <typename T, typename = void>
inline constexpr bool exposesTimeout = false;
template <typename T>
inline constexpr bool exposesTimeout<
    T, std::void_t<decltype(std::declval<T>().timeout)>> = true;

template <typename T, typename = void>
inline constexpr bool exposesPriority = false;
template <typename T>
inline constexpr bool exposesPriority<
    T, std::void_t<decltype(std::declval<T>().priority)>> = true;

static_assert(!exposesTimeout<CommandEnvelope>);
static_assert(!exposesPriority<CommandEnvelope>);

TEST(CommandEnvelopeTest, ContainsOnlyEffectiveCommandContext) {
    const CommandEnvelope command{
        .commandId = CommandId{"command-1"},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .origin = CommandOrigin::Web,
        .expectedStateRevision = 7,
        .payload = CreateWindowCommand{},
    };

    EXPECT_EQ(command.commandId.value(), "command-1");
    EXPECT_EQ(command.sessionId.value(), "session-1");
    EXPECT_EQ(command.instrumentId.value(), "instrument-1");
    EXPECT_EQ(command.origin, CommandOrigin::Web);
    EXPECT_EQ(command.expectedStateRevision, 7U);
    EXPECT_TRUE(std::holds_alternative<CreateWindowCommand>(command.payload));
}

}  // namespace
}  // namespace vna::application
