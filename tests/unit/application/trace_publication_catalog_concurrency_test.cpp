#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <vna/application/command_bus.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application {
namespace {

class ManualGate {
public:
    void open() {
        {
            std::lock_guard lock{mutex_};
            open_ = true;
        }
        changed_.notify_all();
    }

    void wait() {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return open_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool open_{false};
};

StateSnapshot state(display_model::TraceFormat format) {
    return {
        .stateRevision = 7,
        .control = {},
        .instrument = {
            .channels = {{
                .id = domain::ChannelId{1},
                .sweep = {1, 2, 2, 1, 0.0},
            }},
            .measurements = {{
                domain::MeasurementId{1},
                domain::ChannelId{1},
                domain::MeasurementType::S21,
            }},
        },
        .display = {
            .windows = {},
            .traces = {{
                display_model::TraceId{1},
                display_model::WindowId{1},
                domain::MeasurementId{1},
                format,
                std::nullopt,
            }},
        },
    };
}

struct RaceObservation {
    TracePublicationCommitResult phase;
    TracePublicationCommitResult smith;
    display_model::TraceFormat finalFormat;
};

RaceObservation runCommitRace(bool phaseFirst) {
    TraceDisplayFrameRepository repository{2};
    TracePublicationCatalog catalog{
        domain::ChannelId{1},
        repository,
        state(display_model::TraceFormat::LogMagnitude)};
    ManualGate phasePrepared;
    ManualGate smithPrepared;
    ManualGate allowPhase;
    ManualGate allowSmith;
    ManualGate phaseDone;
    ManualGate smithDone;
    TracePublicationCommitResult phase;
    TracePublicationCommitResult smith;
    auto run = [&](display_model::TraceFormat format,
                   ManualGate& prepared,
                   ManualGate& allow,
                   ManualGate& done,
                   TracePublicationCommitResult& result) {
        auto candidate = catalog.prepare(state(format), 8);
        prepared.open();
        allow.wait();
        result = catalog.commit(
            std::get<PreparedTracePublicationPlan>(std::move(candidate)));
        done.open();
    };
    std::thread phaseThread{
        run, display_model::TraceFormat::Phase, std::ref(phasePrepared),
        std::ref(allowPhase),
        std::ref(phaseDone), std::ref(phase)};
    std::thread smithThread{
        run, display_model::TraceFormat::Smith, std::ref(smithPrepared),
        std::ref(allowSmith),
        std::ref(smithDone), std::ref(smith)};
    phasePrepared.wait();
    smithPrepared.wait();
    auto& firstAllow = phaseFirst ? allowPhase : allowSmith;
    auto& firstDone = phaseFirst ? phaseDone : smithDone;
    auto& secondAllow = phaseFirst ? allowSmith : allowPhase;
    firstAllow.open();
    firstDone.wait();
    secondAllow.open();
    phaseThread.join();
    smithThread.join();
    return {phase, smith, catalog.capture()->targets[0].trace.format};
}

void expectStale(const TracePublicationCommitResult& result) {
    ASSERT_TRUE(std::holds_alternative<TracePublicationCatalogError>(result));
    EXPECT_EQ(
        std::get<TracePublicationCatalogError>(result).code,
        TracePublicationCatalogErrorCode::StalePrepared);
}

TEST(TracePublicationCatalogConcurrencyTest, PhaseCommitWinsControlledRace) {
    const auto result = runCommitRace(true);

    EXPECT_TRUE(
        std::holds_alternative<TracePublicationPlanHandle>(result.phase));
    expectStale(result.smith);
    EXPECT_EQ(result.finalFormat, display_model::TraceFormat::Phase);
}

TEST(TracePublicationCatalogConcurrencyTest, SmithCommitWinsReverseRace) {
    const auto result = runCommitRace(false);

    EXPECT_TRUE(
        std::holds_alternative<TracePublicationPlanHandle>(result.smith));
    expectStale(result.phase);
    EXPECT_EQ(result.finalFormat, display_model::TraceFormat::Smith);
}

}  // namespace
}  // namespace vna::application
