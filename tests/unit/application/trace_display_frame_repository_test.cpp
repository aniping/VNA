#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

#include <vna/application/trace_display_frame_repository.hpp>

namespace vna::application {
namespace {

static_assert(noexcept(
    std::declval<TraceDisplayFrameRepository&>().discard(
        display_model::TraceId{1})));

TraceDisplayFrame validFrame(
    display_model::TraceId traceId = display_model::TraceId{3},
    frames::FrameId frameId = frames::FrameId{11},
    std::uint64_t sequenceNumber = 5) {
    return TraceDisplayFrame{
        .frameId = frameId,
        .traceId = traceId,
        .stateRevision = 19,
        .sequenceNumber = sequenceNumber,
        .format = display_model::TraceFormat::LogMagnitude,
        .valueUnit = display_model::ScaleUnit::Decibel,
        .frequenciesHz = {1'000'000.0, 1'500'000.0, 2'000'000.0},
        .values = {-6.020599913, -12.0, -3.0},
    };
}

void expectPublishError(
    TraceDisplayFrameRepository& repository,
    TraceDisplayFrame frame,
    TraceDisplayFrameErrorCode code) {
    const auto result = repository.publish(std::move(frame));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, code);
}

TEST(TraceDisplayFrameRepositoryTest, PublishesAndReturnsLatestImmutableFrame) {
    TraceDisplayFrameRepository repository{2};

    const auto result = repository.publish(validFrame());

    ASSERT_TRUE(result.hasValue());
    const auto latest = repository.latest(display_model::TraceId{3});
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(result.value(), latest);
    EXPECT_EQ(latest->frameId, frames::FrameId{11});
    EXPECT_EQ(latest->stateRevision, 19U);
    EXPECT_EQ(latest->sequenceNumber, 5U);
    EXPECT_EQ(latest->format, display_model::TraceFormat::LogMagnitude);
    EXPECT_EQ(latest->valueUnit, display_model::ScaleUnit::Decibel);
    EXPECT_EQ(latest->frequenciesHz.size(), 3U);
    EXPECT_DOUBLE_EQ(latest->values[0], -6.020599913);
}

TEST(TraceDisplayFrameRepositoryTest, RejectsZeroCapacity) {
    EXPECT_THROW(TraceDisplayFrameRepository{0}, std::invalid_argument);
}

TEST(TraceDisplayFrameRepositoryTest, RejectsZeroFrameTraceAndSequenceIds) {
    TraceDisplayFrameRepository repository{2};
    auto missingFrame = validFrame();
    missingFrame.frameId = frames::FrameId{0};
    expectPublishError(
        repository, std::move(missingFrame),
        TraceDisplayFrameErrorCode::InvalidFrameId);
    auto missingTrace = validFrame();
    missingTrace.traceId = display_model::TraceId{0};
    expectPublishError(
        repository, std::move(missingTrace),
        TraceDisplayFrameErrorCode::InvalidTraceId);
    auto missingSequence = validFrame();
    missingSequence.sequenceNumber = 0;
    expectPublishError(
        repository, std::move(missingSequence),
        TraceDisplayFrameErrorCode::InvalidSequenceNumber);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
}

TEST(TraceDisplayFrameRepositoryTest, RejectsUnsupportedFormatAndValueUnit) {
    TraceDisplayFrameRepository repository{2};
    auto phase = validFrame();
    phase.format = display_model::TraceFormat::Phase;
    expectPublishError(
        repository, std::move(phase),
        TraceDisplayFrameErrorCode::UnsupportedFormat);
    auto invalidUnit = validFrame();
    invalidUnit.valueUnit = static_cast<display_model::ScaleUnit>(99);
    expectPublishError(
        repository, std::move(invalidUnit),
        TraceDisplayFrameErrorCode::UnsupportedValueUnit);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
}

TEST(TraceDisplayFrameRepositoryTest, RejectsInvalidPointCountsAndMismatch) {
    TraceDisplayFrameRepository repository{2};
    auto tooShort = validFrame();
    tooShort.frequenciesHz.resize(1);
    tooShort.values.resize(1);
    expectPublishError(
        repository, std::move(tooShort),
        TraceDisplayFrameErrorCode::InvalidPointCount);
    auto tooLong = validFrame();
    tooLong.frequenciesHz.assign(frames::kMaxSweepPoints + 1, 1.0);
    tooLong.values.assign(frames::kMaxSweepPoints + 1, 1.0);
    expectPublishError(
        repository, std::move(tooLong),
        TraceDisplayFrameErrorCode::InvalidPointCount);
    auto mismatch = validFrame();
    mismatch.values.pop_back();
    expectPublishError(
        repository, std::move(mismatch),
        TraceDisplayFrameErrorCode::SampleCountMismatch);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
}

TEST(TraceDisplayFrameRepositoryTest, RejectsNonFiniteOrUnorderedSamples) {
    TraceDisplayFrameRepository repository{2};
    auto nonFiniteFrequency = validFrame();
    nonFiniteFrequency.frequenciesHz[1] =
        std::numeric_limits<double>::infinity();
    expectPublishError(
        repository, std::move(nonFiniteFrequency),
        TraceDisplayFrameErrorCode::NonFiniteValue);
    auto nonFiniteValue = validFrame();
    nonFiniteValue.values[1] = std::numeric_limits<double>::quiet_NaN();
    expectPublishError(
        repository, std::move(nonFiniteValue),
        TraceDisplayFrameErrorCode::NonFiniteValue);
    auto duplicateFrequency = validFrame();
    duplicateFrequency.frequenciesHz[1] = 1'000'000.0;
    expectPublishError(
        repository, std::move(duplicateFrequency),
        TraceDisplayFrameErrorCode::FrequencyNotStrictlyIncreasing);
    auto decreasingFrequency = validFrame();
    decreasingFrequency.frequenciesHz[1] = 900'000.0;
    expectPublishError(
        repository, std::move(decreasingFrequency),
        TraceDisplayFrameErrorCode::FrequencyNotStrictlyIncreasing);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
}

TEST(TraceDisplayFrameRepositoryTest, ReplaysSameFrameAndSequenceIdempotently) {
    TraceDisplayFrameRepository repository{1};
    const auto first = repository.publish(validFrame());
    ASSERT_TRUE(first.hasValue());
    auto replay = validFrame();
    replay.values[0] = -99.0;

    const auto repeated = repository.publish(std::move(replay));

    ASSERT_TRUE(repeated.hasValue());
    EXPECT_EQ(repeated.value(), first.value());
    EXPECT_DOUBLE_EQ(repository.latest(display_model::TraceId{3})->values[0],
                     -6.020599913);
}

TEST(TraceDisplayFrameRepositoryTest, RejectsSequenceRegressionAtomically) {
    TraceDisplayFrameRepository repository{1};
    const auto current = repository.publish(validFrame());
    ASSERT_TRUE(current.hasValue());

    expectPublishError(
        repository,
        validFrame(display_model::TraceId{3}, frames::FrameId{12}, 4),
        TraceDisplayFrameErrorCode::SequenceRegression);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), current.value());
    expectPublishError(
        repository,
        validFrame(display_model::TraceId{3}, frames::FrameId{12}, 5),
        TraceDisplayFrameErrorCode::SequenceRegression);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), current.value());
}

TEST(TraceDisplayFrameRepositoryTest, EnforcesCapacityWithoutBlockingReplacement) {
    TraceDisplayFrameRepository repository{1};
    const auto first = repository.publish(validFrame());
    ASSERT_TRUE(first.hasValue());

    expectPublishError(
        repository,
        validFrame(display_model::TraceId{4}, frames::FrameId{21}, 1),
        TraceDisplayFrameErrorCode::CapacityExceeded);
    EXPECT_EQ(repository.latest(display_model::TraceId{4}), nullptr);
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), first.value());

    const auto replacement = repository.publish(
        validFrame(display_model::TraceId{3}, frames::FrameId{12}, 6));
    ASSERT_TRUE(replacement.hasValue());
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), replacement.value());
    EXPECT_EQ(first.value()->sequenceNumber, 5U);
    EXPECT_EQ(replacement.value()->sequenceNumber, 6U);
}

TEST(TraceDisplayFrameRepositoryTest, KeepsMaximumFramesIndependentPerTrace) {
    TraceDisplayFrameRepository repository{2};
    auto maximum = validFrame();
    maximum.frequenciesHz.resize(frames::kMaxSweepPoints);
    maximum.values.assign(frames::kMaxSweepPoints, -3.0);
    for (std::size_t index = 0; index < frames::kMaxSweepPoints; ++index) {
        maximum.frequenciesHz[index] = 1'000'000.0 + index;
    }
    const auto first = repository.publish(std::move(maximum));
    const auto second = repository.publish(
        validFrame(display_model::TraceId{4}, frames::FrameId{21}, 1));
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());

    const auto replacement = repository.publish(
        validFrame(display_model::TraceId{3}, frames::FrameId{12}, 6));

    ASSERT_TRUE(replacement.hasValue());
    EXPECT_EQ(repository.latest(display_model::TraceId{3}), replacement.value());
    EXPECT_EQ(repository.latest(display_model::TraceId{4}), second.value());
    EXPECT_EQ(first.value()->frequenciesHz.size(), 2048U);
}

TEST(TraceDisplayFrameRepositoryTest, DiscardReleasesCapacityAndKeepsReaders) {
    TraceDisplayFrameRepository repository{1};
    const auto first = repository.publish(validFrame());
    ASSERT_TRUE(first.hasValue());
    const auto reader = first.value();

    repository.discard(display_model::TraceId{3});
    repository.discard(display_model::TraceId{3});

    EXPECT_EQ(repository.latest(display_model::TraceId{3}), nullptr);
    EXPECT_EQ(reader->traceId, display_model::TraceId{3});
    EXPECT_DOUBLE_EQ(reader->values[0], -6.020599913);
    const auto replacement = repository.publish(
        validFrame(display_model::TraceId{4}, frames::FrameId{21}, 1));
    EXPECT_TRUE(replacement.hasValue());
}

}  // namespace
}  // namespace vna::application
