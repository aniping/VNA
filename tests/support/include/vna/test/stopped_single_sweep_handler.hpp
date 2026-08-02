#pragma once

#include <cstddef>
#include <utility>

#include <vna/application/command_bus.hpp>
#include <vna/application/factory_preset.hpp>
#include <vna/application/single_sweep_command_handler.hpp>
#include <vna/application/sweep_runtime.hpp>
#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::test {

class StoppedSingleSweepExecution final
    : public application::SingleSweepExecution {
public:
    application::SingleSweepSubmitResult submit(
        application::SingleSweepWorkItem) override {
        return application::SingleSweepSubmitError{
            .code = application::SingleSweepSubmitErrorCode::Stopped};
    }

    void invalidateTraceFrame(display_model::TraceId) noexcept override {}
    void discardTrace(display_model::TraceId) noexcept override {}
};

// Existing command tests declare their intentionally inactive sweep boundary
// explicitly; production composition never receives this stopped handler.
inline application::SingleSweepCommandHandler& stoppedSingleSweepHandler() {
    static StoppedSingleSweepExecution execution;
    static application::SingleSweepCommandHandler handler{execution};
    return handler;
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
              {acquisition::test_support::validPlan(), catalog_.capture(), 2,
               {.mode = domain::SweepMode::Single, .sweepCount = 1}},
              [](const acquisition::RawSweepCaptureRequest&,
                 const acquisition::RawSweepChunkObserver&,
                 std::stop_token) {
                  return acquisition::RawSweepCaptureResult{
                      acquisition::RawSweepCaptureCanceled{}};
              },
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
              stoppedSingleSweepHandler(),
              CommandBusRuntimeOwner::catalog(),
              idempotencyCapacity) {}

    StoppedCommandBus(
        application::InstrumentId instrumentId,
        application::CommandBusInitialState initialState,
        std::size_t idempotencyCapacity = 1024)
        : CommandBusRuntimeOwner{initialState},
          CommandBus(
              std::move(instrumentId),
              stoppedSingleSweepHandler(),
              CommandBusRuntimeOwner::catalog(),
              std::move(initialState),
              idempotencyCapacity) {}
};

}  // namespace vna::test
