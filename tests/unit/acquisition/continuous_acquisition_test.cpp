#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

#include <vna/acquisition/continuous_acquisition.hpp>

namespace vna::acquisition {
namespace {

using namespace std::chrono_literals;

ContinuousAcquisitionPlan validPlan() {
    return ContinuousAcquisitionPlan{
        .frequencyAxis = frames::FrequencyAxis{
            .id = frames::FrequencyAxisId{1},
            .startFrequencyHz = 1'000'000,
            .stopFrequencyHz = 2'000'000,
            .points = 3,
        },
        .portCount = 2,
        .sourcePorts = {1, 2},
    };
}

frames::RawReceiverPayload validPayload(std::uint64_t sequence) {
    frames::RawReceiverPayload payload{.portCount = 2};
    for (const auto sourcePort : {1U, 2U}) {
        frames::RawSourceState state{.sourcePort = sourcePort};
        for (int point = 0; point < 3; ++point) {
            const auto value = static_cast<double>(sequence * 10 + point);
            state.samples.push_back(frames::RawReceiverSample{
                .reference = {.real = value, .imaginary = 0.0},
                .responses = {{.real = value + 1.0, .imaginary = 0.0},
                              {.real = value + 2.0, .imaginary = 0.0}},
            });
        }
        payload.sourceStates.push_back(std::move(state));
    }
    return payload;
}

class ControlledSource {
public:
    ControlledSource() : state_(std::make_shared<State>()) {}

    frames::Result<frames::RawReceiverPayload> operator()(
        const ContinuousAcquisitionPlan&,
        std::uint64_t sequence,
        std::stop_token token) const {
        std::stop_callback notify{token, [&] { state_->changed.notify_all(); }};
        std::unique_lock lock{state_->mutex};
        state_->requestedSequence = sequence;
        state_->changed.notify_all();
        state_->changed.wait(lock, [&] {
            return token.stop_requested() || state_->releasedSequence >= sequence;
        });
        return frames::Result<frames::RawReceiverPayload>{validPayload(sequence)};
    }

    bool waitForRequest(std::uint64_t sequence) const {
        std::unique_lock lock{state_->mutex};
        return state_->changed.wait_for(lock, 2s, [&] {
            return state_->requestedSequence >= sequence;
        });
    }

    void release(std::uint64_t sequence) const {
        std::lock_guard lock{state_->mutex};
        state_->releasedSequence = sequence;
        state_->changed.notify_all();
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        std::uint64_t requestedSequence{0};
        std::uint64_t releasedSequence{0};
    };
    std::shared_ptr<State> state_;
};

TEST(ContinuousAcquisitionTest, StartsAndPublishesCompleteFramesInSequence) {
    ControlledSource source;
    ContinuousAcquisition acquisition{validPlan(), source};

    ASSERT_TRUE(source.waitForRequest(1));
    source.release(1);
    const auto first = acquisition.waitForNext(0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->context.frameId, FrameId{1});
    EXPECT_EQ(first->context.sweepId, SweepId{1});
    EXPECT_EQ(first->context.sequenceNumber, 1U);
    EXPECT_EQ(first->frequencyAxis.id, frames::FrequencyAxisId{1});
    EXPECT_EQ(first->frequencyAxis.startFrequencyHz, 1'000'000U);
    EXPECT_EQ(first->frequencyAxis.stopFrequencyHz, 2'000'000U);
    EXPECT_EQ(first->frequencyAxis.points, 3U);
    EXPECT_EQ(first->payload, validPayload(1));

    ASSERT_TRUE(source.waitForRequest(2));
    source.release(2);
    const auto second = acquisition.waitForNext(1);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->context.frameId, FrameId{2});
    EXPECT_EQ(second->context.sweepId, SweepId{2});
    EXPECT_EQ(second->context.sequenceNumber, 2U);
    EXPECT_EQ(acquisition.latest(), second);
    EXPECT_EQ(first->payload, validPayload(1));

    ASSERT_TRUE(source.waitForRequest(3));
    acquisition.stop();
    const auto stopped = acquisition.snapshot();
    EXPECT_EQ(stopped.state, ContinuousAcquisitionState::Stopped);
    EXPECT_EQ(stopped.lastPublishedSequence, 2U);
    EXPECT_EQ(acquisition.latest(), second);
}

}  // namespace
}  // namespace vna::acquisition
