#pragma once

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include <vna/application/trace_display_frame.hpp>

namespace vna::application {

// A set is one complete display publication for a configuration generation.
// Keeping frames together prevents consumers from combining different sweeps.
struct TraceDisplayFrameSet {
    std::uint64_t generation;
    std::uint64_t sequenceNumber;
    std::vector<TraceDisplayFrame> frames;
    friend bool operator==(
        const TraceDisplayFrameSet& left,
        const TraceDisplayFrameSet& right) {
        return left.generation == right.generation &&
            left.sequenceNumber == right.sequenceNumber &&
            left.frames == right.frames;
    }
    friend bool operator!=(
        const TraceDisplayFrameSet& left,
        const TraceDisplayFrameSet& right) {
        return !(left == right);
    }
};

using TraceDisplayFrameSetHandle =
    std::shared_ptr<const TraceDisplayFrameSet>;

struct GenerationAdvanced {
    std::uint64_t generation;
};

struct TraceDisplayFrameSetCursor {
    std::uint64_t generation;
    std::uint64_t sequenceNumber;
};

struct FrameSetAvailable {
    TraceDisplayFrameSetHandle frameSet;
};

using TraceDisplayFrameSetEvent =
    std::variant<FrameSetAvailable, GenerationAdvanced>;

}  // namespace vna::application
