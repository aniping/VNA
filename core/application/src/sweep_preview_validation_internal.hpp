#pragma once

#include <optional>

#include <vna/application/sweep_preview_exchange.hpp>

namespace vna::application::internal {

[[nodiscard]] std::optional<SweepPreviewError> validateSweepPreview(
    const SweepPreview& preview);
[[nodiscard]] bool isCumulativeExtension(
    const SweepPreview& current,
    const SweepPreview& next);

}  // namespace vna::application::internal
