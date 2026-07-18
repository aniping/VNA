#pragma once

#include "vna/runtime/operation_runtime.hpp"
#include "vna/store/instrument_store.hpp"

#include <array>
#include <cstddef>

namespace vna::instrument {

enum class SweepAdmissionErrc {
    ControllerCapacityExhausted,
    RuntimeAdmissionRejected,
    StoreAdmissionRejected,
    StoreInitialCommitRejected,
    RuntimeDispatchContractViolation
};

struct SweepAdmissionError final {
    SweepAdmissionErrc code{SweepAdmissionErrc::ControllerCapacityExhausted};
};

struct AcceptedSweepOperation final {
    store::OperationId operation{};
    runtime::WorkId work{};
};

class SweepCompletionSink {
public:
    virtual ~SweepCompletionSink() = default;
    virtual void on_sweep_terminal(
        store::OperationSnapshot operation) noexcept = 0;
};

class SweepAdmissionController final : private runtime::RuntimeCompletionSink {
public:
    static constexpr std::size_t kMaximumMappings = 16U;

    SweepAdmissionController(
        runtime::OperationRuntime& runtime,
        store::InstrumentStore& store) noexcept;

    core::Result<AcceptedSweepOperation, SweepAdmissionError> submit(
        store::OperationId operation,
        runtime::WorkId work_id,
        runtime::RuntimeWork& work,
        SweepCompletionSink& completion) noexcept;

private:
    struct Mapping final {
        bool active{false};
        runtime::WorkId work{};
        store::OperationId operation{};
        SweepCompletionSink* completion{nullptr};
    };

    void on_runtime_terminal(
        runtime::WorkId work,
        runtime::RuntimeTerminal terminal) noexcept override;
    std::size_t find_free_mapping() const noexcept;
    std::size_t find_mapping(runtime::WorkId work) const noexcept;

    runtime::OperationRuntime& runtime_;
    store::InstrumentStore& store_;
    std::array<Mapping, kMaximumMappings> mappings_{};
};

}  // namespace vna::instrument
