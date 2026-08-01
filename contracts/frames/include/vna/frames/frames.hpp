#pragma once

#include <vector>

#include <vna/domain/instrument.hpp>
#include <vna/frames/raw_receiver.hpp>

namespace vna::frames {

using FrameId = EntityId<struct FrameIdTag>;
using SweepId = EntityId<struct SweepIdTag>;

// The coordinator supplies identity and state correlation so acquisition
// backends cannot publish frames detached from the command state they used.
struct FrameContext {
    FrameId frameId;
    SweepId sweepId;
    domain::ChannelId channelId;
    std::uint64_t stateRevision;
    std::uint64_t sequenceNumber;
};

struct RawReceiverFrame {
    FrameContext context;
    FrequencyAxis frequencyAxis;
    RawReceiverPayload payload;
};

// MeasurementFrame owns synthesized complex values, not display-formatted
// values. The first slice accepts S11 only and makes S21 rejection explicit.
struct MeasurementFrame {
    FrameContext context;
    FrequencyAxis frequencyAxis;
    domain::MeasurementId measurementId;
    domain::MeasurementType type;
    std::vector<ComplexSample> samples;
};

[[nodiscard]] Result<RawReceiverFrame> makeRawReceiverFrame(
    FrameContext context,
    FrequencyAxis frequencyAxis,
    RawReceiverPayload payload);

[[nodiscard]] Result<MeasurementFrame> makeMeasurementFrame(
    FrameContext context,
    FrequencyAxis frequencyAxis,
    domain::MeasurementId measurementId,
    domain::MeasurementType type,
    std::vector<ComplexSample> samples);

}  // namespace vna::frames
