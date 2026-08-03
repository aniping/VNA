#include "sweep_runtime_internal.hpp"

#include <optional>
#include <stdexcept>
#include <variant>

namespace vna::application::internal {

void SweepRuntimeImpl::observePreviewRange(
    SweepPreviewAssembler& assembler,
    bool& previewRejected,
    SweepPreviewIdentity identity,
    const acquisition::RawSweepPointRange& range) {
    std::optional<SweepPreviewAssemblyResult> assembled;
    if (!previewRejected) {
        assembled.emplace(assembler.append(range));
    }
    std::lock_guard lock{mutex_};
    if (!activeIdentity_.has_value() || *activeIdentity_ != identity) {
        return;
    }
    const auto count = static_cast<std::uint64_t>(range.samples.size());
    if (count > snapshot_.progress.totalPoints -
                    snapshot_.progress.completedPoints) {
        throw std::logic_error{"Sweep progress exceeds complete workload"};
    }
    setDisplayStatusLocked(
        SweepUserPhase::Sweeping,
        identity.sweepId,
        snapshot_.progress.completedPoints + count);
    if (previewRejected) {
        previews_.updateForRuntime(displayStatusLocked());
        return;
    }
    bool rejected{};
    if (const auto* preview = std::get_if<SweepPreview>(&*assembled)) {
        const auto published = previews_.publishForRuntime(
            *preview, displayStatusLocked());
        rejected = std::holds_alternative<SweepPreviewError>(published);
    } else if (std::holds_alternative<SweepPreviewAssemblyError>(*assembled)) {
        rejected = true;
    } else {
        previews_.updateForRuntime(displayStatusLocked());
    }
    if (rejected) {
        previewRejected = true;
        ++snapshot_.previewRejectedSweeps;
        invalidateLocked(identity);
    }
}

}  // namespace vna::application::internal
