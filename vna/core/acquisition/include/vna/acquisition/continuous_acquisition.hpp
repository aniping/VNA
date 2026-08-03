#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <vna/compat/stop_token.hpp>
#include <variant>
#include <vector>

#include <vna/frames/raw_receiver.hpp>

namespace vna::acquisition {
using FrameId = frames::EntityId<struct AcquisitionFrameIdTag>;
using SweepId = frames::EntityId<struct AcquisitionSweepIdTag>;

// The plan describes hardware work only. Business identities and display
// choices are attached by downstream consumers, never by the acquisition loop.
struct ContinuousAcquisitionPlan {
    frames::FrequencyAxis frequencyAxis;
    std::uint32_t portCount;
    std::vector<std::uint32_t> sourcePorts;
    std::uint32_t ifBandwidthHz;
    double powerDbm;
    std::chrono::steady_clock::duration minimumSweepPeriod{};
};

struct RawFrameContext {
    FrameId frameId;
    SweepId sweepId;
    std::uint64_t sequenceNumber;
};
struct RawFrame {
    RawFrameContext context;
    frames::FrequencyAxis frequencyAxis;
    frames::RawReceiverPayload payload;
};

using RawFrameHandle = std::shared_ptr<const RawFrame>;
// A production backing source is exclusive to one engine even though this
// callable wrapper is copyable. It must return promptly after stop is requested
// so stop() can join without a second worker driving the same hardware.
using RawSweepSource = std::function<frames::Result<frames::RawReceiverPayload>(
    const ContinuousAcquisitionPlan&,
    std::uint64_t,
    vna::compat::StopToken)>;
enum class ContinuousAcquisitionState {
    Running,
    Stopped,
    Failed,
};
enum class ContinuousAcquisitionFailureCode {
    SourceFailed,
    RawFrameRejected,
    UnexpectedFailure,
};
using ContinuousAcquisitionFailureCause =
    std::variant<std::monostate, frames::FrameError, std::exception_ptr>;

struct ContinuousAcquisitionFailure {
    ContinuousAcquisitionFailureCode code;
    std::uint64_t attemptedSequence;
    ContinuousAcquisitionFailureCause cause;
};
struct ContinuousAcquisitionSnapshot {
    ContinuousAcquisitionState state{ContinuousAcquisitionState::Running};
    std::uint64_t lastPublishedSequence{0};
    std::optional<ContinuousAcquisitionFailure> failure;
};
class ContinuousAcquisition final {
public:
    // Construction starts the sole worker. A zero period delegates pacing to
    // the blocking source; otherwise the plan supplies the minimum cadence.
    ContinuousAcquisition(
        ContinuousAcquisitionPlan plan,
        RawSweepSource source);
    ~ContinuousAcquisition();

    ContinuousAcquisition(const ContinuousAcquisition&) = delete;
    ContinuousAcquisition& operator=(const ContinuousAcquisition&) = delete;
    ContinuousAcquisition(ContinuousAcquisition&&) = delete;
    ContinuousAcquisition& operator=(ContinuousAcquisition&&) = delete;

    // Lifecycle methods are owner-serialized and must not be called by source.
    // stop requests source release and joins; dependencies must outlive it.
    void stop() noexcept;
    void join();

    [[nodiscard]] RawFrameHandle latest() const;
    [[nodiscard]] RawFrameHandle waitForNext(
        std::uint64_t afterSequence,
        vna::compat::StopToken token = {}) const;
    [[nodiscard]] ContinuousAcquisitionSnapshot snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vna::acquisition
