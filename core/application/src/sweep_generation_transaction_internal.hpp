#pragma once

#include <variant>

#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application::internal {

enum class SweepGenerationCommitError {
    StalePrepared,
    GenerationMismatch,
};

using SweepGenerationCommitResult = std::variant<
    TracePublicationPlanHandle,
    SweepGenerationCommitError>;

// This private transaction is the sole multi-module generation committer.
// Its preparation may allocate; once mutation starts, every operation is
// statically constrained to be non-throwing.
class SweepGenerationTransaction {
public:
    [[nodiscard]] static SweepGenerationCommitResult commit(
        TracePublicationCatalog& catalog,
        SweepPreviewExchange& previews,
        PreparedTracePublicationPlan& prepared);

private:
    [[nodiscard]] static bool canAdvance(
        const TraceDisplayFrameRepository& repository,
        const SweepPreviewExchange& previews,
        std::uint64_t current,
        std::uint64_t next) noexcept;
    [[nodiscard]] static SweepGenerationCommitResult advance(
        TracePublicationCatalog& catalog,
        SweepPreviewExchange& previews,
        std::unique_lock<std::mutex>& catalogLock,
        TracePublicationPlanHandle candidate);
};

}  // namespace vna::application::internal
