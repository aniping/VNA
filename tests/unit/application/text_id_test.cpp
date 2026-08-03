#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <vna/application/command_bus.hpp>
#include <vna/application/operation_manager.hpp>
#include <vna/application/single_sweep_executor.hpp>

namespace vna::application {
namespace {

static_assert(std::is_nothrow_copy_constructible_v<CommandId>);
static_assert(std::is_nothrow_move_constructible_v<CommandId>);
static_assert(std::is_nothrow_move_assignable_v<CommandId>);
#if __GNUC__ > 7
static_assert(std::is_nothrow_copy_constructible_v<CommandEnvelope>);
static_assert(std::is_nothrow_copy_constructible_v<CommandResult>);
#endif
static_assert(std::is_nothrow_move_constructible_v<CommandResult>);
static_assert(std::is_nothrow_copy_constructible_v<OperationSnapshot>);
static_assert(std::is_nothrow_move_constructible_v<OperationSnapshot>);
static_assert(std::is_nothrow_move_constructible_v<OperationResult>);
static_assert(std::is_nothrow_copy_constructible_v<OperationFailure>);
static_assert(std::is_nothrow_copy_constructible_v<OperationFailed>);
static_assert(std::is_nothrow_move_constructible_v<OperationTerminalOutcome>);
static_assert(std::is_same_v<
              std::variant_alternative_t<0, SingleSweepSubmitResult>,
              OperationId>);
static_assert(std::is_nothrow_move_constructible_v<SingleSweepSubmitResult>);

std::string textWithByte(unsigned char byte) {
    std::string value{"a"};
    value.push_back(static_cast<char>(byte));
    value.push_back('b');
    return value;
}

TEST(TextIdTest, RejectsEmptyValue) {
    EXPECT_THROW(
        static_cast<void>(CommandId{""}),
        std::invalid_argument);
}

TEST(TextIdTest, RejectsMoreThan128Bytes) {
    EXPECT_THROW(
        static_cast<void>(CommandId{std::string(129, 'a')}),
        std::invalid_argument);
}

TEST(TextIdTest, RejectsAsciiControlBytes) {
    for (unsigned int byte = 0; byte <= 0x1F; ++byte) {
        SCOPED_TRACE(byte);
        EXPECT_THROW(
            static_cast<void>(CommandId{
                textWithByte(static_cast<unsigned char>(byte))}),
            std::invalid_argument);
    }
    EXPECT_THROW(
        static_cast<void>(CommandId{textWithByte(0x7F)}),
        std::invalid_argument);
}

TEST(TextIdTest, AcceptsBoundaryLengths) {
    const CommandId oneByte{"a"};
    const SessionId maximum{std::string(128, 'a')};

    EXPECT_EQ(oneByte.value(), "a");
    EXPECT_EQ(maximum.value().size(), 128U);
}

TEST(TextIdTest, AcceptsNonAsciiBytesWithoutNormalization) {
    const std::string value{static_cast<char>(0x80), static_cast<char>(0xFF)};

    const InstrumentId id{value};

    EXPECT_EQ(id.value(), value);
}

TEST(TextIdTest, RvalueOperationsPreserveSourceAndTargetInvariants) {
    CommandId constructionSource{"command-1"};
    const CommandId constructed{std::move(constructionSource)};
    SessionId assignmentSource{"session-1"};
    SessionId assigned{"session-before"};

    assigned = std::move(assignmentSource);

    EXPECT_EQ(constructionSource.value(), "command-1");
    EXPECT_EQ(constructed.value(), "command-1");
    EXPECT_EQ(assignmentSource.value(), "session-1");
    EXPECT_EQ(assigned.value(), "session-1");
}

}  // namespace
}  // namespace vna::application
