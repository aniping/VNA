#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

#include <vna/application/trace_display_frame_repository.hpp>
#include <vna/display_model/display_workspace.hpp>
#include <vna/domain/instrument.hpp>

namespace vna::application {

namespace internal {
class SweepGenerationTransaction;
class SweepRuntimeImpl;
}

struct StateSnapshot;

struct TracePublicationTarget {
    domain::MeasurementSnapshot measurement;
    display_model::TraceSnapshot trace;
};

// A captured plan is immutable so a publisher can process outside the gate.
// Its generation is the only publication admission token.
struct TracePublicationPlan {
    std::uint64_t generation;
    std::uint64_t stateRevision;
    domain::ChannelId channelId;
    std::vector<TracePublicationTarget> targets;
};

using TracePublicationPlanHandle =
    std::shared_ptr<const TracePublicationPlan>;

enum class TracePublicationCatalogErrorCode {
    MeasurementNotFound,
    ChannelNotFound,
    DuplicateTraceId,
    UnsupportedMeasurementType,
    UnsupportedTraceFormat,
    InvalidPlanHandle,
    StalePrepared,
    StalePublication,
    GenerationOverflow,
    RepositoryRejected,
};

struct TracePublicationCatalogError {
    TracePublicationCatalogErrorCode code;
    std::optional<TraceDisplayFrameSetError> repositoryError;
};

class PreparedTracePublicationPlan {
private:
    friend class TracePublicationCatalog;
    friend class internal::SweepGenerationTransaction;
    friend class internal::SweepRuntimeImpl;
    // The immutable base token rejects out-of-order non-material commits even
    // when both candidates intentionally keep the same generation.
    PreparedTracePublicationPlan(
        TracePublicationPlanHandle basePlan,
        TracePublicationPlanHandle candidate);

    TracePublicationPlanHandle basePlan_;
    TracePublicationPlanHandle candidate_;
};

using TracePublicationPrepareResult = std::variant<
    PreparedTracePublicationPlan,
    TracePublicationCatalogError>;
using TracePublicationCommitResult = std::variant<
    TracePublicationPlanHandle,
    TracePublicationCatalogError>;
using TracePublicationPublishResult = std::variant<
    TraceDisplayFrameSetHandle,
    TracePublicationCatalogError>;

class TracePublicationCatalog {
public:
    // Dependencies outlive the catalog. The initial snapshot is the trusted
    // composition-root state and starts in repository generation one.
    TracePublicationCatalog(
        domain::ChannelId acquisitionChannelId,
        TraceDisplayFrameRepository& repository,
        const StateSnapshot& initialState);

    [[nodiscard]] TracePublicationPrepareResult prepare(
        const StateSnapshot& candidate,
        std::uint64_t nextStateRevision) const;
    [[nodiscard]] TracePublicationCommitResult commit(
        PreparedTracePublicationPlan prepared);
    [[nodiscard]] TracePublicationPlanHandle capture() const;
    [[nodiscard]] TracePublicationPublishResult publishIfCurrent(
        const TracePublicationPlanHandle& plan,
        TraceDisplayFrameSet frameSet);

private:
    friend class internal::SweepGenerationTransaction;
    domain::ChannelId acquisitionChannelId_;
    TraceDisplayFrameRepository& repository_;
    mutable std::mutex mutex_;
    TracePublicationPlanHandle current_;
};

}  // namespace vna::application
