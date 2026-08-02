#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <variant>

#include <spdlog/spdlog.h>

#include <vna/application/command_bus.hpp>
#include <vna/application/continuous_trace_publisher.hpp>
#include <vna/application/trace_publication_catalog.hpp>
#include <vna/test/captured_runtime_log.hpp>
#include <vna/test/continuous_acquisition_test_support.hpp>

namespace vna::application {
namespace {
using namespace std::chrono_literals;

StateSnapshot traceState(
    std::uint64_t revision,
    display_model::TraceFormat format) {
    return {
        .stateRevision = revision,
        .control = {},
        .instrument = {
            .channels = {{domain::ChannelId{1}, {1'000'000, 2'000'000, 3,
                                                  10'000, -10.5}}},
            .measurements = {{domain::MeasurementId{1}, domain::ChannelId{1},
                              domain::MeasurementType::S21}}},
        .display = {.traces = {{
            display_model::TraceId{1}, display_model::WindowId{1},
            domain::MeasurementId{1}, format, std::nullopt}}},
    };
}

class SetWait {
public:
    SetWait(
        const TraceDisplayFrameRepository& repository,
        TraceDisplayFrameSetCursor cursor)
        : returned_(promise_.get_future()),
          worker_([this, &repository, cursor](std::stop_token token) {
              result_ = repository.waitForNextSet(cursor, token);
              promise_.set_value();
          }) {}
    ~SetWait() { worker_.request_stop(); }

    TraceDisplayFrameSetHandle finish() {
        if (returned_.wait_for(2s) != std::future_status::ready) {
            worker_.request_stop();
            throw std::runtime_error{"frame set was not published"};
        }
        worker_.join();
        if (!result_.has_value()) {
            return nullptr;
        }
        const auto* available = std::get_if<FrameSetAvailable>(&*result_);
        return available == nullptr ? nullptr : available->frameSet;
    }

private:
    std::promise<void> promise_;
    std::future<void> returned_;
    std::optional<TraceDisplayFrameSetEvent> result_;
    std::jthread worker_;
};

TracePublicationPlanHandle advancePlan(
    TracePublicationCatalog& catalog,
    std::uint64_t revision) {
    auto prepared = catalog.prepare(
        traceState(revision, display_model::TraceFormat::Phase), revision);
    if (!std::holds_alternative<PreparedTracePublicationPlan>(prepared)) {
        return nullptr;
    }
    auto committed = catalog.commit(
        std::get<PreparedTracePublicationPlan>(std::move(prepared)));
    const auto* plan = std::get_if<TracePublicationPlanHandle>(&committed);
    return plan == nullptr ? nullptr : *plan;
}

TEST(ContinuousTraceLogTest, AttemptsOnlyFirstPublishedFramePerGeneration) {
    spdlog::drop("vna");
    acquisition::test_support::ControlledSource source;
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository,
        traceState(7, display_model::TraceFormat::LogMagnitude)};
    ContinuousTracePublisher publisher{acquisition, catalog};

    SetWait first{repository, {1, 0}};
    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    ASSERT_NE(first.finish(), nullptr);
    vna::test::CapturedRuntimeLog log;
    SetWait second{repository, {1, 1}};
    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    ASSERT_NE(second.finish(), nullptr);
    ASSERT_NE(advancePlan(catalog, 8), nullptr);
    SetWait third{repository, {2, 0}};
    ASSERT_TRUE(source.waitForRequest(3));
    source.release(3);
    ASSERT_NE(third.finish(), nullptr);
    SetWait fourth{repository, {2, 3}};
    ASSERT_TRUE(source.waitForRequest(4));
    source.release(4);
    ASSERT_NE(fourth.finish(), nullptr);
    publisher.stop();
    acquisition.stop();

    const auto expected =
        "DEBUG [连续扫频] 已发布配置代次首个完整显示帧 | generation=2 "
        "| revision=8 | frame_id=3 | sweep_id=3 | sequence=3 | "
        "trace_count=1";
    EXPECT_EQ(log.count(expected), 1U);
    EXPECT_EQ(log.count("generation=1"), 0U);
}

TEST(ContinuousTraceLogTest, RecordsAcquisitionFailureOnce) {
    vna::test::CapturedRuntimeLog log;
    const auto source = [](
                            const acquisition::ContinuousAcquisitionPlan&,
                            std::uint64_t,
                            std::stop_token) {
        return frames::Result<frames::RawReceiverPayload>{
            frames::FrameError{frames::FrameErrorCode::InvalidPortCount}};
    };
    acquisition::ContinuousAcquisition acquisition{
        acquisition::test_support::validPlan(), source};
    TraceDisplayFrameRepository repository{1};
    TracePublicationCatalog catalog{
        domain::ChannelId{1}, repository,
        traceState(7, display_model::TraceFormat::LogMagnitude)};
    ContinuousTracePublisher publisher{acquisition, catalog};
    acquisition.join();
    publisher.join();

    EXPECT_EQ(log.count(
        "ERROR [连续扫频] 持续采集已停止 | attempted_sequence=1 | "
        "error_code=source-failed"), 1U);
    EXPECT_EQ(repository.latestFrameSet(), nullptr);
}

}  // namespace
}  // namespace vna::application
