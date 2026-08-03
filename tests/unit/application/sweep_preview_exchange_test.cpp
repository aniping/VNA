#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <variant>
#include <vector>

#include <vna/application/sweep_preview_exchange.hpp>
#include <vna/test/sweep_status_test_support.hpp>

namespace vna::application {
namespace {

using namespace std::chrono_literals;

SweepPreview previewFor(
    std::uint64_t sequence,
    std::vector<double> values) {
    return {
        .identity = {1, acquisition::SweepId{7}},
        .channelId = domain::ChannelId{1},
        .stateRevision = 4,
        .sequenceNumber = sequence,
        .totalPointCount = 4,
        .traces = {{
            .traceId = display_model::TraceId{1},
            .measurementId = domain::MeasurementId{1},
            .measurementType = domain::MeasurementType::S21,
            .format = display_model::TraceFormat::LogMagnitude,
            .frequenciesHz = {1.0e6, 2.0e6, 3.0e6},
            .samples = CartesianTraceDisplaySamples{
                .unit = TraceDisplayUnit::Decibel,
                .values = std::move(values)},
        }},
    };
}

class PreviewWaitCall {
public:
    PreviewWaitCall(
        const SweepPreviewExchange& exchange,
        SweepPreviewCursor cursor)
        : entered_(enteredPromise_.get_future()),
          returned_(returnedPromise_.get_future()),
          worker_([this, &exchange, cursor](std::stop_token token) {
              enteredPromise_.set_value();
              result_ = exchange.waitForNext(cursor, token);
              returnedPromise_.set_value();
          }) {}

    ~PreviewWaitCall() {
        worker_.request_stop();
    }

    [[nodiscard]] bool hasEntered() {
        return entered_.wait_for(2s) == std::future_status::ready;
    }

    void requestStop() {
        worker_.request_stop();
    }

    std::optional<SweepPreviewEvent> finish() {
        if (returned_.wait_for(2s) != std::future_status::ready) {
            worker_.request_stop();
            throw std::runtime_error{"preview wait did not finish"};
        }
        worker_.join();
        return result_;
    }

private:
    std::promise<void> enteredPromise_;
    std::future<void> entered_;
    std::promise<void> returnedPromise_;
    std::future<void> returned_;
    std::optional<SweepPreviewEvent> result_;
    std::jthread worker_;
};

TEST(SweepPreviewExchangeTest, SlowReaderReceivesLatestCumulativePreview) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    auto first = previewFor(11, {-1.0, -2.0, -3.0});
    first.traces.front().frequenciesHz.resize(2);
    std::get<CartesianTraceDisplaySamples>(first.traces.front().samples)
        .values.resize(2);

    const auto firstResult = exchange.publish(std::move(first));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(firstResult));
    const auto firstHandle = std::get<SweepPreviewHandle>(firstResult);
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        exchange.publish(previewFor(11, {-1.0, -2.0, -3.0}))));

    const auto event = exchange.waitForNext(SweepPreviewCursor{});

    ASSERT_TRUE(event.has_value());
    const auto* available = std::get_if<SweepPreviewAvailable>(&*event);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(available->cursor.value, 3U);
    EXPECT_EQ(available->preview->traces.front().frequenciesHz.size(), 3U);
    EXPECT_EQ(firstHandle->traces.front().frequenciesHz.size(), 2U);
}

TEST(SweepPreviewExchangeTest, GenerationAdvanceInvalidatesWithoutNewPreview) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        exchange.publish(previewFor(11, {-1.0, -2.0, -3.0}))));
    EXPECT_TRUE(exchange.invalidate({1, acquisition::SweepId{7}}));

    const auto advanced = exchange.advanceGeneration(2);

    ASSERT_TRUE(std::holds_alternative<SweepPreviewGenerationAdvanced>(
        advanced));
    EXPECT_EQ(
        std::get<SweepPreviewGenerationAdvanced>(advanced).cursor.value,
        4U);
    const auto event = exchange.waitForNext(SweepPreviewCursor{1});
    ASSERT_TRUE(event.has_value());
    const auto* generation =
        std::get_if<SweepPreviewGenerationAdvanced>(&*event);
    ASSERT_NE(generation, nullptr);
    EXPECT_EQ(generation->generation, 2U);
    EXPECT_TRUE(generation->status.runtime.firstSweepAfterConfiguration);
    EXPECT_EQ(generation->status.activePreviewIdentity, std::nullopt);

    auto stale = previewFor(12, {-1.0, -2.0, -3.0});
    stale.identity.sweepId = acquisition::SweepId{8};
    const auto rejected = exchange.publish(std::move(stale));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(rejected));
    EXPECT_EQ(
        std::get<SweepPreviewError>(rejected).code,
        SweepPreviewErrorCode::StaleGeneration);
}

TEST(SweepPreviewExchangeTest, GenerationAdvanceWakesEmptyExchangeWaiter) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    PreviewWaitCall wait{exchange, {1}};
    ASSERT_TRUE(wait.hasEntered());

    const auto advanced = exchange.advanceGeneration(2);
    const auto event = wait.finish();

    ASSERT_TRUE(std::holds_alternative<SweepPreviewGenerationAdvanced>(
        advanced));
    ASSERT_TRUE(event.has_value());
    const auto* generation =
        std::get_if<SweepPreviewGenerationAdvanced>(&*event);
    ASSERT_NE(generation, nullptr);
    EXPECT_EQ(generation->cursor.value, 2U);
    EXPECT_EQ(generation->generation, 2U);
}

TEST(SweepPreviewExchangeTest, InvalidationSealsSweepUntilNewIdentity) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    const SweepPreviewIdentity firstIdentity{1, acquisition::SweepId{7}};
    ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        exchange.publish(previewFor(11, {-1.0, -2.0, -3.0}))));

    EXPECT_TRUE(exchange.invalidate(firstIdentity));
    EXPECT_FALSE(exchange.invalidate(firstIdentity));
    const auto event = exchange.waitForNext(SweepPreviewCursor{2});
    ASSERT_TRUE(event.has_value());
    const auto* invalidated = std::get_if<SweepPreviewInvalidated>(&*event);
    ASSERT_NE(invalidated, nullptr);
    EXPECT_EQ(invalidated->identity, firstIdentity);
    EXPECT_EQ(invalidated->cursor.value, 3U);

    const auto late = exchange.publish(previewFor(11, {-1.0, -2.0, -3.0}));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(late));
    EXPECT_EQ(
        std::get<SweepPreviewError>(late).code,
        SweepPreviewErrorCode::SweepIdRegression);
    auto next = previewFor(12, {-4.0, -5.0, -6.0});
    next.identity.sweepId = acquisition::SweepId{8};
    EXPECT_TRUE(std::holds_alternative<SweepPreviewHandle>(
        exchange.publish(std::move(next))));
}

TEST(SweepPreviewExchangeTest, RejectsFutureAndSkippedGeneration) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    auto future = previewFor(11, {-1.0, -2.0, -3.0});
    future.identity.generation = 2;

    const auto futureResult = exchange.publish(std::move(future));
    ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(futureResult));
    EXPECT_EQ(
        std::get<SweepPreviewError>(futureResult).code,
        SweepPreviewErrorCode::FutureGeneration);
    const auto skipped = exchange.advanceGeneration(3);
    ASSERT_TRUE(std::holds_alternative<SweepPreviewError>(skipped));
    EXPECT_EQ(
        std::get<SweepPreviewError>(skipped).code,
        SweepPreviewErrorCode::GenerationNotNext);
}

TEST(SweepPreviewExchangeTest, CancellationReturnsNoEvent) {
    SweepPreviewExchange exchange{vna::test::testSweepStatus()};
    std::stop_source alreadyStopped;
    alreadyStopped.request_stop();
    EXPECT_EQ(
        exchange.waitForNext({}, alreadyStopped.get_token()),
        std::nullopt);

    PreviewWaitCall wait{exchange, {1}};
    ASSERT_TRUE(wait.hasEntered());
    wait.requestStop();
    EXPECT_EQ(wait.finish(), std::nullopt);
}

TEST(SweepPreviewExchangeTest, PublishAtWaitRegistrationCannotBeLost) {
    for (std::uint64_t sequence = 1; sequence <= 50; ++sequence) {
        SweepPreviewExchange exchange{vna::test::testSweepStatus()};
        PreviewWaitCall wait{exchange, {1}};
        ASSERT_TRUE(wait.hasEntered());
        ASSERT_TRUE(std::holds_alternative<SweepPreviewHandle>(
            exchange.publish(previewFor(
                sequence, {-1.0, -2.0, -3.0}))));

        const auto event = wait.finish();

        ASSERT_TRUE(event.has_value());
        const auto* available = std::get_if<SweepPreviewAvailable>(&*event);
        ASSERT_NE(available, nullptr);
        EXPECT_EQ(available->preview->sequenceNumber, sequence);
    }
}

}  // namespace
}  // namespace vna::application
