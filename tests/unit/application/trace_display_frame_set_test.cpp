#include <gtest/gtest.h>

#include <limits>
#include <utility>
#include <variant>

#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::application {
namespace {

TraceDisplayFrame frame(
    std::uint64_t traceId,
    domain::MeasurementType type,
    std::uint64_t sequence) {
    return {
        .frameId = frames::FrameId{100 + sequence},
        .traceId = display_model::TraceId{traceId},
        .measurementId = domain::MeasurementId{traceId},
        .measurementType = type,
        .stateRevision = 7,
        .generation = 1,
        .sequenceNumber = sequence,
        .format = display_model::TraceFormat::LogMagnitude,
        .frequenciesHz = {1.0e6, 2.0e6},
        .samples = CartesianTraceDisplaySamples{
            .unit = TraceDisplayUnit::Decibel,
            .values = {-static_cast<double>(traceId), -6.0}},
    };
}

TraceDisplayFrameSet frameSet(
    std::uint64_t sequence,
    std::uint64_t generation = 1) {
    auto first = frame(1, domain::MeasurementType::S12, sequence);
    auto second = frame(2, domain::MeasurementType::S22, sequence);
    first.generation = generation;
    second.generation = generation;
    return {
        .generation = generation,
        .sequenceNumber = sequence,
        .frames = {std::move(first), std::move(second)},
    };
}

const TraceDisplayFrameSetHandle& expectSet(
    const TraceDisplayFrameSetResult& result) {
    const auto* handle = std::get_if<TraceDisplayFrameSetHandle>(&result);
    EXPECT_NE(handle, nullptr);
    static const TraceDisplayFrameSetHandle empty;
    return handle == nullptr ? empty : *handle;
}

void expectError(
    const TraceDisplayFrameSetResult& result,
    TraceDisplayFrameSetErrorCode code) {
    const auto* error = std::get_if<TraceDisplayFrameSetError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, code);
}

TEST(TraceDisplayFrameSetTest, PublishesAndAtomicallyReplacesCompleteSet) {
    TraceDisplayFrameRepository repository{2};
    const auto firstResult = repository.publishFrameSet(frameSet(1));
    const auto first = expectSet(firstResult);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(repository.latestFrameSet(), first);
    EXPECT_EQ(repository.latest(display_model::TraceId{1})->sequenceNumber, 1U);
    EXPECT_EQ(repository.latest(display_model::TraceId{2})->sequenceNumber, 1U);
    auto replacement = frameSet(2);
    std::get<CartesianTraceDisplaySamples>(replacement.frames[0].samples)
        .values[0] = -20.0;

    const auto secondResult = repository.publishFrameSet(std::move(replacement));
    const auto second = expectSet(secondResult);

    ASSERT_NE(second, nullptr);
    EXPECT_EQ(repository.latestFrameSet(), second);
    EXPECT_EQ(repository.latest(display_model::TraceId{1})->sequenceNumber, 2U);
    EXPECT_EQ(repository.latest(display_model::TraceId{2})->sequenceNumber, 2U);
    EXPECT_EQ(first->sequenceNumber, 1U);
    EXPECT_DOUBLE_EQ(
        std::get<CartesianTraceDisplaySamples>(first->frames[0].samples)
            .values[0],
        -1.0);
}

TEST(TraceDisplayFrameSetTest, ReplaysExactSetAndRejectsSequenceConflicts) {
    TraceDisplayFrameRepository repository{2};
    const auto initial = repository.publishFrameSet(frameSet(2));
    const auto retained = expectSet(initial);
    ASSERT_NE(retained, nullptr);

    EXPECT_EQ(expectSet(repository.publishFrameSet(frameSet(2))), retained);
    auto conflict = frameSet(2);
    std::get<CartesianTraceDisplaySamples>(conflict.frames[0].samples)
        .values[0] = -99.0;
    expectError(
        repository.publishFrameSet(std::move(conflict)),
        TraceDisplayFrameSetErrorCode::SequenceConflict);
    expectError(
        repository.publishFrameSet(frameSet(1)),
        TraceDisplayFrameSetErrorCode::SequenceRegression);
    EXPECT_EQ(repository.latestFrameSet(), retained);
}

TEST(TraceDisplayFrameSetTest, AdvancesGenerationExactlyAndRejectsWrongEpoch) {
    TraceDisplayFrameRepository repository{2};
    const auto retained = expectSet(repository.publishFrameSet(frameSet(1)));
    ASSERT_NE(retained, nullptr);
    const auto skipped = repository.advanceGeneration(3);
    EXPECT_EQ(
        std::get<TraceDisplayFrameSetError>(skipped).code,
        TraceDisplayFrameSetErrorCode::GenerationNotNext);
    EXPECT_EQ(repository.latestFrameSet(), retained);

    const auto advanced = repository.advanceGeneration(2);

    EXPECT_EQ(std::get<GenerationAdvanced>(advanced).generation, 2U);
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
    EXPECT_EQ(repository.latest(display_model::TraceId{1}), nullptr);
    expectError(
        repository.publishFrameSet(frameSet(2, 1)),
        TraceDisplayFrameSetErrorCode::StaleGeneration);
    expectError(
        repository.publishFrameSet(frameSet(2, 3)),
        TraceDisplayFrameSetErrorCode::FutureGeneration);
    EXPECT_NE(expectSet(repository.publishFrameSet(frameSet(2, 2))), nullptr);
}

TEST(TraceDisplayFrameSetTest, RejectsInvalidSetWithoutReplacingLastGood) {
    TraceDisplayFrameRepository repository{2};
    const auto retained = expectSet(repository.publishFrameSet(frameSet(1)));
    ASSERT_NE(retained, nullptr);
    expectError(
        repository.publishFrameSet(TraceDisplayFrameSet{1, 2, {}}),
        TraceDisplayFrameSetErrorCode::EmptyFrameSet);
    auto duplicate = frameSet(2);
    duplicate.frames[1].traceId = duplicate.frames[0].traceId;
    expectError(
        repository.publishFrameSet(std::move(duplicate)),
        TraceDisplayFrameSetErrorCode::DuplicateTraceId);
    auto mismatched = frameSet(2);
    mismatched.frames[1].stateRevision = 8;
    expectError(
        repository.publishFrameSet(std::move(mismatched)),
        TraceDisplayFrameSetErrorCode::FrameMetadataMismatch);
    auto wrongGeneration = frameSet(2);
    wrongGeneration.frames[1].generation = 2;
    expectError(
        repository.publishFrameSet(std::move(wrongGeneration)),
        TraceDisplayFrameSetErrorCode::FrameMetadataMismatch);
    auto wrongSequence = frameSet(2);
    wrongSequence.frames[1].sequenceNumber = 3;
    expectError(
        repository.publishFrameSet(std::move(wrongSequence)),
        TraceDisplayFrameSetErrorCode::FrameMetadataMismatch);
    auto wrongFrame = frameSet(2);
    wrongFrame.frames[1].frameId = frames::FrameId{99};
    expectError(
        repository.publishFrameSet(std::move(wrongFrame)),
        TraceDisplayFrameSetErrorCode::FrameMetadataMismatch);
    auto wrongFrequencies = frameSet(2);
    wrongFrequencies.frames[1].frequenciesHz[1] = 3.0e6;
    expectError(
        repository.publishFrameSet(std::move(wrongFrequencies)),
        TraceDisplayFrameSetErrorCode::FrameMetadataMismatch);
    auto invalid = frameSet(2);
    std::get<CartesianTraceDisplaySamples>(invalid.frames[0].samples)
        .values[0] = std::numeric_limits<double>::quiet_NaN();
    const auto invalidResult = repository.publishFrameSet(std::move(invalid));
    expectError(invalidResult, TraceDisplayFrameSetErrorCode::InvalidFrame);
    EXPECT_EQ(
        std::get<TraceDisplayFrameSetError>(invalidResult).frameError->code,
        TraceDisplayFrameErrorCode::NonFiniteValue);
    EXPECT_EQ(repository.latestFrameSet(), retained);
}

TEST(TraceDisplayFrameSetTest, AppliesCapacityToTheWholeReplacementSet) {
    TraceDisplayFrameRepository tooSmall{1};
    expectError(
        tooSmall.publishFrameSet(frameSet(1)),
        TraceDisplayFrameSetErrorCode::CapacityExceeded);
    EXPECT_EQ(tooSmall.latestFrameSet(), nullptr);
    TraceDisplayFrameRepository exact{2};
    ASSERT_NE(expectSet(exact.publishFrameSet(frameSet(1))), nullptr);
    ASSERT_NE(expectSet(exact.publishFrameSet(frameSet(2))), nullptr);
}

}  // namespace
}  // namespace vna::application
