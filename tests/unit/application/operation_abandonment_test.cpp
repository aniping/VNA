#include <gtest/gtest.h>

#include <variant>

#include <vna/application/operation_manager.hpp>

namespace vna::application {
namespace {

TEST(OperationAbandonmentTest, CancelsQueuedOperationAndSatisfiesFence) {
    OperationManager manager;
    const auto created = manager.create(OperationSubmission{
        CommandId{"command-1"}, SessionId{"session-1"}, 7});
    bool satisfied = false;
    auto subscription = manager.subscribe(
        manager.captureFence(created.sessionId),
        [&] { satisfied = true; });

    manager.abandonQueued(created.id);

    const auto snapshot =
        std::get<OperationSnapshot>(manager.snapshot(created.id));
    EXPECT_TRUE(std::holds_alternative<OperationCanceled>(snapshot.state));
    EXPECT_TRUE(satisfied);
}

}  // namespace
}  // namespace vna::application
