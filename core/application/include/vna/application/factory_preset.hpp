#pragma once

#include <cstdint>

#include <vna/acquisition/continuous_acquisition.hpp>
#include <vna/display_model/display_workspace.hpp>
#include <vna/domain/instrument.hpp>

namespace vna::application {

struct ContinuousTracePreset {
    std::uint64_t stateRevision;
    domain::MeasurementSnapshot measurement;
    display_model::TraceSnapshot trace;
};

// A complete state is assembled before CommandBus construction so no caller
// can observe partially-created factory entities or artificial revisions.
struct CommandBusInitialState {
    domain::Instrument instrument;
    display_model::DisplayWorkspace displayWorkspace;
};

// Hardware work and business state share one product profile, while the raw
// acquisition plan deliberately remains free of application entity IDs.
struct FactoryPreset {
    acquisition::ContinuousAcquisitionPlan acquisitionPlan;
    CommandBusInitialState commandBusState;
    ContinuousTracePreset continuousTracePreset;
};

[[nodiscard]] FactoryPreset makeFactoryPreset();

}  // namespace vna::application
