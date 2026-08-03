#include <vna/application/trace_publication_catalog.hpp>

#include <vna/application/command_bus.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace vna::application {
namespace {

using TargetCompilation = std::variant<
    std::vector<TracePublicationTarget>,
    TracePublicationCatalogError>;
using TargetResolution = std::variant<
    std::optional<TracePublicationTarget>,
    TracePublicationCatalogError>;

TracePublicationCatalogError catalogError(
    TracePublicationCatalogErrorCode code) {
    return {.code = code, .repositoryError = std::nullopt};
}

bool supports(domain::MeasurementType type) {
    switch (type) {
        case domain::MeasurementType::S11:
        case domain::MeasurementType::S21:
        case domain::MeasurementType::S12:
        case domain::MeasurementType::S22:
            return true;
    }
    return false;
}

bool supports(display_model::TraceFormat format) {
    switch (format) {
        case display_model::TraceFormat::LogMagnitude:
        case display_model::TraceFormat::Phase:
        case display_model::TraceFormat::Smith:
            return true;
    }
    return false;
}

TargetResolution resolveTarget(
    domain::ChannelId acquisitionChannelId,
    const StateSnapshot& state,
    const display_model::TraceSnapshot& trace) {
    const auto measurement = std::find_if(
        state.instrument.measurements.cbegin(),
        state.instrument.measurements.cend(),
        [&trace](const auto& item) { return item.id == trace.measurementId; });
    if (measurement == state.instrument.measurements.cend()) {
        return catalogError(
            TracePublicationCatalogErrorCode::MeasurementNotFound);
    }
    const auto channel = std::find_if(
        state.instrument.channels.cbegin(),
        state.instrument.channels.cend(),
        [measurement](const auto& item) {
            return item.id == measurement->channelId;
        });
    if (channel == state.instrument.channels.cend()) {
        return catalogError(TracePublicationCatalogErrorCode::ChannelNotFound);
    }
    if (channel->id != acquisitionChannelId) {
        return std::optional<TracePublicationTarget>{};
    }
    if (!supports(measurement->type)) {
        return catalogError(
            TracePublicationCatalogErrorCode::UnsupportedMeasurementType);
    }
    if (!supports(trace.format)) {
        return catalogError(
            TracePublicationCatalogErrorCode::UnsupportedTraceFormat);
    }
    return std::optional<TracePublicationTarget>{{*measurement, trace}};
}

TargetCompilation compileTargets(
    domain::ChannelId acquisitionChannelId,
    const StateSnapshot& state) {
    // Resolve every reference before filtering by Channel so corrupt state
    // cannot hide behind an otherwise-ignored acquisition target.
    std::vector<TracePublicationTarget> targets;
    std::unordered_set<std::uint64_t> traceIds;
    for (const auto& trace : state.display.traces) {
        if (!traceIds.insert(trace.id.value()).second) {
            return catalogError(
                TracePublicationCatalogErrorCode::DuplicateTraceId);
        }
        auto resolved = resolveTarget(acquisitionChannelId, state, trace);
        if (const auto* error =
                std::get_if<TracePublicationCatalogError>(&resolved)) {
            return *error;
        }
        auto target =
            std::get<std::optional<TracePublicationTarget>>(resolved);
        if (target.has_value()) {
            targets.push_back(std::move(*target));
        }
    }
    std::sort(
        targets.begin(), targets.end(), [](const auto& left, const auto& right) {
            return left.trace.id.value() < right.trace.id.value();
        });
    return targets;
}

bool sameMaterialIdentity(
    const TracePublicationPlan& current,
    const std::vector<TracePublicationTarget>& candidates) {
    if (current.targets.size() != candidates.size()) {
        return false;
    }
    return std::equal(
        current.targets.cbegin(),
        current.targets.cend(),
        candidates.cbegin(),
        [](const auto& left, const auto& right) {
            return left.trace.id == right.trace.id &&
                   left.measurement.id == right.measurement.id &&
                   left.measurement.type == right.measurement.type &&
                   left.trace.format == right.trace.format;
        });
}

}  // namespace

PreparedTracePublicationPlan::PreparedTracePublicationPlan(
    TracePublicationPlanHandle basePlan,
    TracePublicationPlanHandle candidate)
    : basePlan_(std::move(basePlan)), candidate_(std::move(candidate)) {}

TracePublicationCatalog::TracePublicationCatalog(
    domain::ChannelId acquisitionChannelId,
    TraceDisplayFrameRepository& repository,
    const StateSnapshot& initialState)
    : acquisitionChannelId_(acquisitionChannelId),
      repository_(repository) {
    auto compiled = compileTargets(acquisitionChannelId, initialState);
    if (const auto* error =
            std::get_if<TracePublicationCatalogError>(&compiled)) {
        static_cast<void>(error);
        throw std::invalid_argument{"invalid initial publication state"};
    }
    current_ = std::make_shared<const TracePublicationPlan>(
        TracePublicationPlan{
            .generation = 1,
            .stateRevision = initialState.stateRevision,
            .channelId = acquisitionChannelId,
            .targets = std::move(
                std::get<std::vector<TracePublicationTarget>>(compiled)),
        });
}

TracePublicationPlanHandle TracePublicationCatalog::capture() const {
    std::lock_guard lock{mutex_};
    return current_;
}

TracePublicationPrepareResult TracePublicationCatalog::prepare(
    const StateSnapshot& candidate,
    std::uint64_t nextStateRevision,
    bool forceGenerationAdvance) const {
    // Only copying the immutable base token needs the gate; all candidate
    // validation and allocation intentionally happen after capture returns.
    const auto base = capture();
    auto compiled = compileTargets(acquisitionChannelId_, candidate);
    if (const auto* error =
            std::get_if<TracePublicationCatalogError>(&compiled)) {
        return *error;
    }
    auto targets =
        std::move(std::get<std::vector<TracePublicationTarget>>(compiled));
    const auto changed = forceGenerationAdvance ||
        !sameMaterialIdentity(*base, targets);
    if (changed &&
        base->generation == std::numeric_limits<std::uint64_t>::max()) {
        return catalogError(
            TracePublicationCatalogErrorCode::GenerationOverflow);
    }
    const auto generation = base->generation + (changed ? 1U : 0U);
    auto plan = std::make_shared<const TracePublicationPlan>(
        TracePublicationPlan{
            .generation = generation,
            .stateRevision = nextStateRevision,
            .channelId = acquisitionChannelId_,
            .targets = std::move(targets),
        });
    return PreparedTracePublicationPlan{base, std::move(plan)};
}

TracePublicationCommitResult TracePublicationCatalog::commit(
    PreparedTracePublicationPlan prepared) {
    // Keep repository advance and plan publication in one linearization gate.
    std::lock_guard lock{mutex_};
    if (prepared.basePlan_.get() != current_.get()) {
        return catalogError(
            TracePublicationCatalogErrorCode::StalePrepared);
    }
    auto candidate = prepared.candidate_;
    if (candidate->generation != current_->generation) {
        const auto advanced =
            repository_.advanceGeneration(candidate->generation);
        if (const auto* error =
                std::get_if<TraceDisplayFrameSetError>(&advanced)) {
            return TracePublicationCatalogError{
                .code =
                    TracePublicationCatalogErrorCode::RepositoryRejected,
                .repositoryError = *error,
            };
        }
    }
    current_ = std::move(candidate);
    return current_;
}

TracePublicationPublishResult TracePublicationCatalog::publishIfCurrent(
    const TracePublicationPlanHandle& plan,
    TraceDisplayFrameSet frameSet) {
    if (plan == nullptr) {
        return catalogError(
            TracePublicationCatalogErrorCode::InvalidPlanHandle);
    }
    // The gate closes the check-then-publish race with generation advance.
    std::lock_guard lock{mutex_};
    if (plan->generation != current_->generation) {
        return catalogError(
            TracePublicationCatalogErrorCode::StalePublication);
    }
    auto published = repository_.publishFrameSet(std::move(frameSet));
    if (const auto* error =
            std::get_if<TraceDisplayFrameSetError>(&published)) {
        return TracePublicationCatalogError{
            .code = TracePublicationCatalogErrorCode::RepositoryRejected,
            .repositoryError = *error,
        };
    }
    return std::get<TraceDisplayFrameSetHandle>(std::move(published));
}

}  // namespace vna::application
