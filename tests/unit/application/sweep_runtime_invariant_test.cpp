#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>
#include <vna/test/sweep_status_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;
acquisition::RawSweepCaptureSource gatedSource(
    std::shared_future<void> released) {
    return [released](
               const acquisition::RawSweepCaptureRequest& request,
               const acquisition::RawSweepChunkObserver& observer,
               std::stop_token) ->
        acquisition::RawSweepCaptureResult {
        auto payload = acquisition::test_support::validPayload(request.sequenceNumber);
        const auto& sourceState = payload.sourceStates.front();
        observer({sourceState.sourcePort, 0,
                  {sourceState.samples.cbegin(), sourceState.samples.cbegin() + 2}});
        released.wait();
        return payload;
    };
}

std::optional<SweepPreviewEvent> waitBounded(
    SweepPreviewExchange& previews, SweepPreviewCursor cursor) {
    std::stop_source stop;
    auto waiting = std::async(std::launch::async, [&] {
        return previews.waitForNext(cursor, stop.get_token());
    });
    if (waiting.wait_for(2s) != std::future_status::ready) {
        stop.request_stop();
    }
    return waiting.get();
}
class SweepRuntimeInvariantTest : public ::testing::Test {
protected:
    FactoryPreset preset_{makeFactoryPreset()};
    TraceDisplayFrameRepository repository_{4};
    TracePublicationCatalog catalog_{
        preset_.acquisitionChannelId, repository_,
        {0, {}, preset_.commandBusState.instrument.snapshot(),
         preset_.commandBusState.displayWorkspace.snapshot()}};
    SweepPreviewExchange previews_{vna::test::testSweepStatus()};
    OperationManager operations_;
};

TEST_F(SweepRuntimeInvariantTest, OperationMismatchFailsRuntimeWithoutOrphan) {
    std::promise<void> release;
    const auto source = gatedSource(release.get_future().share());
    SweepRuntime runtime{
        {acquisition::test_support::validPlan(), catalog_.capture(), 2,
         {domain::SweepMode::Single, 1}},
        source, previews_, catalog_, operations_};
    const auto operationId = std::get<OperationId>(runtime.requestRestart(
        domain::ChannelId{1}, {
        CommandId{"restart-invariant"}, SessionId{"session-1"}, 5}));
    const auto preview = waitBounded(previews_, SweepPreviewCursor{3});
    auto tampered = OperationResult{OperationError{
        .code = OperationErrorCode::NotFound}};
    if (preview.has_value()) {
        tampered = operations_.complete(
            operationId, OperationSucceeded{frames::FrameId{99}});
    }
    const auto tamperedSuccessfully =
        std::holds_alternative<OperationSnapshot>(tampered);
    release.set_value();
    runtime.stop();
    ASSERT_TRUE(preview.has_value());
    ASSERT_TRUE(tamperedSuccessfully);
    const auto snapshot = runtime.snapshot();
    EXPECT_EQ(snapshot.state, SweepRuntimeState::Failed);
    ASSERT_NE(snapshot.terminalFailure, nullptr);
    try {
        std::rethrow_exception(snapshot.terminalFailure);
    } catch (const std::logic_error& error) {
        EXPECT_NE(std::string{error.what()}.find("InternalInvariantViolation"), std::string::npos);
    }
    const auto terminal = std::get<OperationSnapshot>(operations_.snapshot(operationId));
    EXPECT_TRUE(std::holds_alternative<OperationSucceeded>(terminal.state));
    const auto cursor = std::get<SweepPreviewAvailable>(*preview).cursor;
    const auto invalidated = waitBounded(previews_, cursor);
    ASSERT_TRUE(invalidated.has_value());
    const auto& status = std::visit(
        [](const auto& value) -> const SweepPreviewStreamStatus& {
            return value.status;
        }, *invalidated);
    EXPECT_EQ(status.runtime.userPhase, SweepUserPhase::Failed);
    EXPECT_EQ(status.activePreviewIdentity, std::nullopt);
}
}  // namespace
}  // namespace vna::application
