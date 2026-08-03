#pragma once

#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <utility>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::test {

inline application::FactoryPreset singleSweepFactoryPreset() {
    auto preset = application::makeFactoryPreset();
    const auto updated = preset.commandBusState.instrument.updateChannelSweepControl(
        domain::ChannelId{1}, domain::SweepMode::Single, 1);
    if (!updated.hasValue()) {
        throw std::logic_error{"invalid single-sweep test preset"};
    }
    return preset;
}

inline acquisition::ContinuousAcquisitionPlan commandBusTestPlan(
    const application::CommandBusInitialState& initialState) {
    auto plan = acquisition::test_support::validPlan();
    const auto channels = initialState.instrument.snapshot().channels;
    if (!channels.empty()) {
        const auto& sweep = channels.front().sweep;
        plan.frequencyAxis.startFrequencyHz = sweep.startFrequencyHz;
        plan.frequencyAxis.stopFrequencyHz = sweep.stopFrequencyHz;
        plan.frequencyAxis.points = sweep.points;
        plan.ifBandwidthHz = static_cast<std::uint32_t>(sweep.ifBandwidthHz);
        plan.powerDbm = sweep.powerDbm;
    }
    return plan;
}

inline application::SweepRuntimeExecutionPolicy commandBusTestExecution(
    const application::CommandBusInitialState& initialState) {
    const auto channels = initialState.instrument.snapshot().channels;
    if (channels.empty()) {
        return {.mode = domain::SweepMode::Single, .sweepCount = 1};
    }
    return {
        .mode = channels.front().sweepMode,
        .sweepCount = channels.front().sweepCount,
    };
}

inline acquisition::RawSweepCaptureResult waitUntilRuntimeStops(
    const acquisition::RawSweepCaptureRequest&,
    const acquisition::RawSweepChunkObserver&,
    std::stop_token token) {
    std::mutex mutex;
    std::condition_variable changed;
    std::stop_callback notify{token, [&] {
        std::lock_guard lock{mutex};
        changed.notify_all();
    }};
    std::unique_lock lock{mutex};
    changed.wait(lock, [&] { return token.stop_requested(); });
    return acquisition::RawSweepCaptureCanceled{};
}

// Tests own the same explicit repository/catalog borrowing graph as production.
// Keeping that graph in a base lets the public CommandBus API stay mandatory
// without hiding a fallback catalog inside production code.
class CommandBusRuntimeOwner {
public:
    explicit CommandBusRuntimeOwner(
        const application::CommandBusInitialState& initialState = {},
        std::size_t traceCapacity = 1024)
        : repository_(traceCapacity),
          catalog_(
              domain::ChannelId{1},
              repository_,
              application::StateSnapshot{
                  .stateRevision = 0,
                  .control = {},
                  .instrument = initialState.instrument.snapshot(),
                  .display = initialState.displayWorkspace.snapshot(),
              }),
          runtime_(
              {commandBusTestPlan(initialState), catalog_.capture(), 2,
               commandBusTestExecution(initialState)},
              waitUntilRuntimeStops,
              previews_, catalog_, operations_) {}

    [[nodiscard]] application::TracePublicationCatalog& catalog() noexcept {
        return catalog_;
    }

    [[nodiscard]] application::TraceDisplayFrameRepository& repository()
        noexcept {
        return repository_;
    }

    [[nodiscard]] application::SweepRuntime& runtime() noexcept {
        return runtime_;
    }

    [[nodiscard]] application::OperationManager& operations() noexcept {
        return operations_;
    }

private:
    application::TraceDisplayFrameRepository repository_;
    application::TracePublicationCatalog catalog_;
    application::SweepPreviewExchange previews_;
    application::OperationManager operations_;
    application::SweepRuntime runtime_;
};

class StoppedCommandBus final
    : private CommandBusRuntimeOwner,
      public application::CommandBus {
public:
    explicit StoppedCommandBus(
        application::InstrumentId instrumentId,
        std::size_t idempotencyCapacity = 1024)
        : CommandBusRuntimeOwner{},
          CommandBus(
              std::move(instrumentId),
              CommandBusRuntimeOwner::runtime(),
              idempotencyCapacity) {}

    StoppedCommandBus(
        application::InstrumentId instrumentId,
        application::CommandBusInitialState initialState,
        std::size_t idempotencyCapacity = 1024)
        : CommandBusRuntimeOwner{initialState},
          CommandBus(
              std::move(instrumentId),
              CommandBusRuntimeOwner::runtime(),
              std::move(initialState),
              idempotencyCapacity) {}
};

}  // namespace vna::test
