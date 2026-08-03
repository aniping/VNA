#include <vna/application/sweep_preview_assembler.hpp>

#include "frequency_axis_materialization_internal.hpp"
#include "trace_display_samples_internal.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include <vna/data_plane/trace_projector.hpp>
#include <vna/measurement/s_parameter_synthesizer.hpp>

namespace vna::application {
namespace {

using MeasurementRanges = std::vector<measurement::MeasurementSampleRange>;

SweepPreviewAssemblyError assemblyError(
    SweepPreviewAssemblyErrorCode code,
    std::optional<frames::FrameError> cause = {}) {
    return {.code = code, .cause = cause};
}

std::vector<domain::MeasurementSnapshot> collectMeasurements(
    const TracePublicationPlan& plan) {
    std::vector<domain::MeasurementSnapshot> measurements;
    std::unordered_set<std::uint64_t> seen;
    for (const auto& target : plan.targets) {
        if (seen.insert(target.measurement.id.value()).second) {
            measurements.push_back(target.measurement);
        }
    }
    return measurements;
}

const measurement::MeasurementSampleRange* findMeasurement(
    const MeasurementRanges& ranges,
    domain::MeasurementId id) {
    const auto found = std::find_if(
        ranges.cbegin(), ranges.cend(), [id](const auto& range) {
            return range.measurementId == id;
        });
    return found == ranges.cend() ? nullptr : &*found;
}

bool appendSamples(TraceDisplaySamples& destination,
                   const TraceDisplaySamples& source) {
    return std::visit(
        [&source](auto& current) {
            using Samples = std::decay_t<decltype(current)>;
            const auto* next = std::get_if<Samples>(&source);
            if (next == nullptr || current.unit != next->unit) {
                return false;
            }
            current.values.insert(
                current.values.end(), next->values.cbegin(), next->values.cend());
            return true;
        },
        destination);
}

}  // namespace

class SweepPreviewAssembler::Impl {
public:
    explicit Impl(SweepPreviewAssemblyPlan plan)
        : plan_(std::move(plan)),
          frequencies_(internal::materializeFrequencies(
              plan_.acquisition.frequencyAxis)) {
        validatePlan();
        for (const auto port : plan_.acquisition.sourcePorts) {
            rawBySource_.try_emplace(port);
        }
        measurements_ = collectMeasurements(*plan_.publication);
    }

    SweepPreviewAssemblyResult append(
        const acquisition::RawSweepPointRange& range) {
        if (const auto invalid = validateOrder(range)) {
            return *invalid;
        }
        const auto measured = synthesize(range);
        if (!measured.hasValue()) {
            return assemblyError(
                SweepPreviewAssemblyErrorCode::MeasurementSynthesisFailed,
                measured.error());
        }
        auto candidate = traces_;
        if (const auto invalid = project(range, measured.value(), candidate)) {
            return *invalid;
        }
        // Raw progress and every affected Trace advance together. Committing
        // only after all projections succeed lets the same range be retried.
        auto& raw = rawBySource_.at(range.sourcePort);
        raw.insert(raw.end(), range.samples.cbegin(), range.samples.cend());
        if (measured.value().empty()) {
            return SweepPreviewAssemblyPending{};
        }
        traces_ = std::move(candidate);
        return preview();
    }

private:
    void validatePlan() const {
        if (plan_.publication == nullptr || plan_.publication->generation == 0 ||
            plan_.publication->channelId.value() == 0 ||
            plan_.sweepId.value() == 0 || plan_.sequenceNumber == 0 ||
            plan_.acquisition.frequencyAxis.points < 2 || !frequencies_) {
            throw std::invalid_argument{"invalid preview assembly plan"};
        }
    }

    std::optional<SweepPreviewAssemblyError> validateOrder(
        const acquisition::RawSweepPointRange& range) const {
        const auto found = rawBySource_.find(range.sourcePort);
        if (found == rawBySource_.cend()) {
            return assemblyError(
                SweepPreviewAssemblyErrorCode::UnexpectedSourcePort);
        }
        const auto expected = found->second.size();
        if (range.firstPoint > expected) {
            return assemblyError(SweepPreviewAssemblyErrorCode::RangeGap);
        }
        if (range.firstPoint < expected) {
            return assemblyError(SweepPreviewAssemblyErrorCode::RangeOverlap);
        }
        return std::nullopt;
    }

    frames::Result<MeasurementRanges> synthesize(
        const acquisition::RawSweepPointRange& range) const {
        return measurement::synthesizeSParameterRanges({
            range.sourcePort,
            range.firstPoint,
            plan_.acquisition.frequencyAxis.points,
            plan_.acquisition.portCount,
            range.samples,
            measurements_,
        });
    }

    std::optional<SweepPreviewAssemblyError> project(
        const acquisition::RawSweepPointRange& range,
        const MeasurementRanges& measured,
        std::map<std::uint64_t, SweepTracePreview>& candidate) const {
        for (const auto& target : plan_.publication->targets) {
            const auto* measurement =
                findMeasurement(measured, target.measurement.id);
            if (measurement == nullptr) {
                continue;
            }
            const auto projected = data_plane::projectTraceSamples(
                measurement->samples, target.trace.format);
            if (!projected.hasValue()) {
                return assemblyError(
                    SweepPreviewAssemblyErrorCode::TraceProjectionFailed,
                    projected.error());
            }
            auto samples = internal::toTraceDisplaySamples(projected.value());
            if (!samples || !appendTarget(
                    target, range, std::move(*samples), candidate)) {
                return assemblyError(
                    SweepPreviewAssemblyErrorCode::SamplePayloadMismatch);
            }
        }
        return std::nullopt;
    }

    bool appendTarget(
        const TracePublicationTarget& target,
        const acquisition::RawSweepPointRange& range,
        TraceDisplaySamples samples,
        std::map<std::uint64_t, SweepTracePreview>& candidate) const {
        const auto begin = frequencies_->cbegin() + range.firstPoint;
        const std::vector<double> frequencies{begin, begin + range.samples.size()};
        auto [found, inserted] = candidate.try_emplace(
            target.trace.id.value(),
            SweepTracePreview{target.trace.id, target.measurement.id,
                              target.measurement.type, target.trace.format,
                              frequencies, samples});
        if (inserted) {
            return range.firstPoint == 0;
        }
        found->second.frequenciesHz.insert(
            found->second.frequenciesHz.end(), frequencies.cbegin(), frequencies.cend());
        return appendSamples(found->second.samples, samples);
    }

    SweepPreview preview() const {
        std::vector<SweepTracePreview> traces;
        traces.reserve(traces_.size());
        for (const auto& [id, trace] : traces_) {
            static_cast<void>(id);
            traces.push_back(trace);
        }
        return {
            {plan_.publication->generation, plan_.sweepId},
            plan_.publication->channelId,
            plan_.publication->stateRevision,
            plan_.sequenceNumber,
            plan_.acquisition.frequencyAxis.points,
            std::move(traces),
        };
    }

    SweepPreviewAssemblyPlan plan_;
    std::optional<std::vector<double>> frequencies_;
    std::vector<domain::MeasurementSnapshot> measurements_;
    std::map<std::uint32_t, std::vector<frames::RawReceiverSample>> rawBySource_;
    std::map<std::uint64_t, SweepTracePreview> traces_;
};

SweepPreviewAssembler::SweepPreviewAssembler(SweepPreviewAssemblyPlan plan)
    : impl_(std::make_unique<Impl>(std::move(plan))) {}

SweepPreviewAssembler::~SweepPreviewAssembler() = default;

SweepPreviewAssemblyResult SweepPreviewAssembler::append(
    const acquisition::RawSweepPointRange& range) {
    return impl_->append(range);
}

}  // namespace vna::application
