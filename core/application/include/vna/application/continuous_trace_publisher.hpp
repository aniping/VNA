#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <vna/acquisition/continuous_acquisition.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application {
enum class ContinuousTracePublisherState {
    Running,
    Stopped,
    AcquisitionFailed,
};

struct ContinuousTracePublisherSnapshot {
    ContinuousTracePublisherState state{ContinuousTracePublisherState::Running};
    std::uint64_t observedFrames{};
    std::uint64_t publishedFrames{};
    std::uint64_t rejectedFrames{};
    std::uint64_t lastObservedSequence{};
    std::uint64_t lastPublishedSequence{};
    std::optional<acquisition::ContinuousAcquisitionFailure> acquisitionFailure;
};

// This worker is the sole production waitForNext consumer of acquisition state.
// Acquisition and catalog dependencies outlive it; each raw sweep uses one
// immutable plan capture and produces at most one atomic frame-set publish.
class ContinuousTracePublisher final {
public:
    ContinuousTracePublisher(
        acquisition::ContinuousAcquisition& acquisition,
        TracePublicationCatalog& catalog);
    ~ContinuousTracePublisher();
    ContinuousTracePublisher(const ContinuousTracePublisher&) = delete;
    ContinuousTracePublisher& operator=(const ContinuousTracePublisher&) = delete;
    ContinuousTracePublisher(ContinuousTracePublisher&&) = delete;
    ContinuousTracePublisher& operator=(ContinuousTracePublisher&&) = delete;

    // Owner serializes stop, join, and destruction. stop wakes only this worker;
    // join blocks until acquisition reaches a natural terminal state.
    void stop() noexcept;
    void join();
    [[nodiscard]] ContinuousTracePublisherSnapshot snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vna::application
