#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

class StartGate {
public:
    void arriveAndWait() {
        std::unique_lock lock{mutex_};
        ++arrived_;
        changed_.notify_all();
        changed_.wait(lock, [this] { return released_; });
    }

    void releaseBoth() {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return arrived_ == 2; });
        released_ = true;
        changed_.notify_all();
    }

private:
    int arrived_{0};
    bool released_{false};
    std::mutex mutex_;
    std::condition_variable changed_;
};

CommandEnvelope command(const char* id, domain::MeasurementType type) {
    return {
        .commandId = CommandId{id},
        .sessionId = SessionId{"session-1"},
        .instrumentId = InstrumentId{"instrument-1"},
        .origin = CommandOrigin::Web,
        .expectedStateRevision = 0,
        .payload = SetTraceMeasurementTypeCommand{
            display_model::TraceId{1}, type},
    };
}

TEST(TraceMeasurementTypeCommandConcurrencyTest, DispatchesLinearizeAtRevision) {
    auto preset = makeFactoryPreset();
    vna::test::CommandBusRuntimeOwner runtimeOwner{
        preset.commandBusState, 4};
    CommandBus bus{
        InstrumentId{"instrument-1"},
        runtimeOwner.runtime(),
        std::move(preset.commandBusState)};
    StartGate gate;
    std::optional<CommandResult> first;
    std::optional<CommandResult> second;
    std::thread firstThread{[&] {
        gate.arriveAndWait();
        first = bus.dispatch(command("first", domain::MeasurementType::S11));
    }};
    std::thread secondThread{[&] {
        gate.arriveAndWait();
        second = bus.dispatch(command("second", domain::MeasurementType::S12));
    }};
    gate.releaseBoth();
    firstThread.join();
    secondThread.join();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const auto succeeded = [](const CommandResult& result) {
        return std::holds_alternative<CommandSuccess>(result.outcome);
    };
    EXPECT_NE(succeeded(*first), succeeded(*second));
    const auto& rejected = succeeded(*first) ? *second : *first;
    EXPECT_EQ(
        commandErrorCode(std::get<CommandError>(rejected.outcome)),
        CommandErrorCode::StateRevisionConflict);
    const auto state = bus.snapshot();
    EXPECT_EQ(state.stateRevision, 1U);
    EXPECT_EQ(state.instrument.measurements.size(), 2U);
    EXPECT_EQ(state.display.traces.size(), 1U);
    EXPECT_EQ(runtimeOwner.catalog().capture()->generation, 2U);
}

}  // namespace
}  // namespace vna::application
