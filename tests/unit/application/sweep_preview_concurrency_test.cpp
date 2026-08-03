#include <gtest/gtest.h>

#include <future>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/test/sweep_status_test_support.hpp>

namespace vna::application {
namespace {

SweepPreview previewFor(
    acquisition::SweepId sweepId,
    std::size_t points) {
    std::vector<double> frequencies{1.0e6, 2.0e6, 3.0e6};
    std::vector<double> values{-1.0, -2.0, -3.0};
    frequencies.resize(points);
    values.resize(points);
    return {
        .identity = {1, sweepId},
        .channelId = domain::ChannelId{1},
        .stateRevision = 4,
        .sequenceNumber = sweepId.value(),
        .totalPointCount = 3,
        .traces = {{
            .traceId = display_model::TraceId{1},
            .measurementId = domain::MeasurementId{1},
            .measurementType = domain::MeasurementType::S21,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = std::move(frequencies),
            .samples = CartesianTraceDisplaySamples{
                .unit = TraceDisplayUnit::Decibel,
                .values = std::move(values)},
        }},
    };
}

SweepPreviewCursor invalidatedCursorAfter(
    const SweepPreviewExchange& exchange,
    SweepPreviewCursor after) {
    const auto event = exchange.waitForNext(after);
    return std::get<SweepPreviewInvalidated>(*event).cursor;
}

void expectAvailableCursor(
    const SweepPreviewExchange& exchange,
    SweepPreviewCursor after,
    std::uint64_t expected) {
    const auto event = exchange.waitForNext(after);
    ASSERT_TRUE(event.has_value());
    const auto* available = std::get_if<SweepPreviewAvailable>(&*event);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(available->cursor.value, expected);
}

TEST(SweepPreviewConcurrencyTest, InvalidateLinearizesWithPublish) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        SweepPreviewExchange exchange{vna::test::testSweepStatus()};
        const SweepPreviewIdentity identity{1, acquisition::SweepId{9}};
        ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
            exchange.publish(previewFor(identity.sweepId, 1))));
        std::promise<void> startPromise;
        auto start = startPromise.get_future().share();
        std::optional<SweepPreviewPublishResult> published;
        bool invalidated = false;
        std::thread publisher{[&] {
            start.wait();
            published = exchange.publish(previewFor(identity.sweepId, 2));
        }};
        std::thread invalidator{[&] {
            start.wait();
            invalidated = exchange.invalidate(identity);
        }};

        startPromise.set_value();
        publisher.join();
        invalidator.join();

        ASSERT_TRUE(invalidated);
        ASSERT_TRUE(published.has_value());
        if (const auto* error = std::get_if<SweepPreviewError>(&*published)) {
            EXPECT_EQ(error->code, SweepPreviewErrorCode::SweepIdRegression);
        }
        const auto invalidatedCursor = invalidatedCursorAfter(exchange, {2});
        EXPECT_GE(invalidatedCursor.value, 3U);
        EXPECT_LE(invalidatedCursor.value, 4U);
        const auto late = exchange.publish(previewFor(identity.sweepId, 3));
        ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(late));
        EXPECT_EQ(
            std::get<SweepPreviewError>(late).code,
            SweepPreviewErrorCode::SweepIdRegression);
        const auto next = exchange.publish(
            previewFor(acquisition::SweepId{10}, 2));
        ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(next));
        expectAvailableCursor(
            exchange, invalidatedCursor, invalidatedCursor.value + 1);
    }
}

TEST(SweepPreviewConcurrencyTest, GenerationAdvanceWinsOverOldPublish) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        SweepPreviewExchange exchange{vna::test::testSweepStatus()};
        std::promise<void> startPromise;
        auto start = startPromise.get_future().share();
        std::optional<SweepPreviewPublishResult> published;
        std::optional<SweepPreviewGenerationResult> advanced;
        std::thread publisher{[&] {
            start.wait();
            published = exchange.publish(
                previewFor(acquisition::SweepId{9}, 2));
        }};
        std::thread generation{[&] {
            start.wait();
            advanced = exchange.advanceGeneration(2);
        }};
        startPromise.set_value();
        publisher.join();
        generation.join();
        ASSERT_TRUE(published.has_value());
        ASSERT_TRUE(advanced.has_value());
        if (const auto* error = std::get_if<SweepPreviewError>(&*published)) {
            EXPECT_EQ(error->code, SweepPreviewErrorCode::StaleGeneration);
        }
        ASSERT_TRUE(std::holds_alternative<SweepPreviewGenerationAdvanced>(
            *advanced));
        const auto event = exchange.waitForNext({});
        ASSERT_TRUE(event.has_value());
        const auto* generationEvent =
            std::get_if<SweepPreviewGenerationAdvanced>(&*event);
        ASSERT_NE(generationEvent, nullptr);
        EXPECT_EQ(generationEvent->generation, 2U);
        EXPECT_GE(generationEvent->cursor.value, 2U);
        EXPECT_LE(generationEvent->cursor.value, 3U);
        const auto late = exchange.publish(
            previewFor(acquisition::SweepId{10}, 2));
        ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(late));
        EXPECT_EQ(
            std::get<SweepPreviewError>(late).code,
            SweepPreviewErrorCode::StaleGeneration);
        auto current = previewFor(acquisition::SweepId{10}, 2);
        current.identity.generation = 2;
        ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
            exchange.publish(std::move(current))));
        expectAvailableCursor(
            exchange,
            generationEvent->cursor,
            generationEvent->cursor.value + 1);
    }
}

TEST(SweepPreviewConcurrencyTest, BothInvalidateOrdersHaveExactCursors) {
    SweepPreviewExchange publishFirst{vna::test::testSweepStatus()};
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        publishFirst.publish(previewFor(acquisition::SweepId{9}, 1))));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        publishFirst.publish(previewFor(acquisition::SweepId{9}, 2))));
    ASSERT_TRUE(publishFirst.invalidate({1, acquisition::SweepId{9}}));
    EXPECT_EQ(invalidatedCursorAfter(publishFirst, {3}).value, 4U);

    SweepPreviewExchange invalidateFirst{vna::test::testSweepStatus()};
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        invalidateFirst.publish(previewFor(acquisition::SweepId{9}, 1))));
    ASSERT_TRUE(invalidateFirst.invalidate({1, acquisition::SweepId{9}}));
    EXPECT_TRUE(std::holds_alternative<SweepPreviewError>(
        invalidateFirst.publish(previewFor(acquisition::SweepId{9}, 2))));
    const auto retainedInvalidation = invalidateFirst.waitForNext({2});
    ASSERT_TRUE(retainedInvalidation.has_value());
    EXPECT_EQ(
        std::get<SweepPreviewInvalidated>(*retainedInvalidation).cursor.value,
        3U);
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        invalidateFirst.publish(previewFor(acquisition::SweepId{10}, 2))));
    expectAvailableCursor(invalidateFirst, {3}, 4U);
}

TEST(SweepPreviewConcurrencyTest, BothGenerationOrdersHaveExactCursors) {
    SweepPreviewExchange publishFirst{vna::test::testSweepStatus()};
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        publishFirst.publish(previewFor(acquisition::SweepId{9}, 2))));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewGenerationAdvanced>(
        publishFirst.advanceGeneration(2)));
    auto afterPublish = previewFor(acquisition::SweepId{10}, 2);
    afterPublish.identity.generation = 2;
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        publishFirst.publish(std::move(afterPublish))));
    expectAvailableCursor(publishFirst, {3}, 4U);

    SweepPreviewExchange advanceFirst{vna::test::testSweepStatus()};
    ASSERT_TRUE(std::holds_alternative<SweepPreviewGenerationAdvanced>(
        advanceFirst.advanceGeneration(2)));
    EXPECT_TRUE(std::holds_alternative<SweepPreviewError>(
        advanceFirst.publish(previewFor(acquisition::SweepId{9}, 2))));
    const auto retainedGeneration = advanceFirst.waitForNext({});
    ASSERT_TRUE(retainedGeneration.has_value());
    EXPECT_EQ(
        std::get<SweepPreviewGenerationAdvanced>(
            *retainedGeneration).cursor.value,
        2U);
    auto afterAdvance = previewFor(acquisition::SweepId{10}, 2);
    afterAdvance.identity.generation = 2;
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        advanceFirst.publish(std::move(afterAdvance))));
    expectAvailableCursor(advanceFirst, {2}, 3U);
}

}  // namespace
}  // namespace vna::application
