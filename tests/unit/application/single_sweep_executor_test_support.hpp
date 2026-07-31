#pragma once

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <utility>
#include <variant>

#include <vna/application/single_sweep_executor.hpp>

namespace vna::application::test_support {

using namespace std::chrono_literals;

inline SingleSweepWorkItem validWorkItem(
    CommandId commandId = CommandId{"sweep-1"},
    display_model::TraceId traceId = display_model::TraceId{3}) {
    return SingleSweepWorkItem{
        .commandId = std::move(commandId),
        .sessionId = SessionId{"session-1"},
        .frameContext = {
            .frameId = frames::FrameId{11},
            .sweepId = frames::SweepId{21},
            .channelId = domain::ChannelId{1},
            .stateRevision = 7,
            .sequenceNumber = 1,
        },
        .frequencyAxis = {
            .id = frames::FrequencyAxisId{31},
            .startFrequencyHz = 1'000'000,
            .stopFrequencyHz = 2'000'000,
            .points = 5,
        },
        .measurement = {
            .id = domain::MeasurementId{1},
            .channelId = domain::ChannelId{1},
            .type = domain::MeasurementType::S11,
        },
        .traceId = traceId,
    };
}

inline OperationSnapshot acceptedOperation(
    OperationManager& manager,
    SingleSweepSubmitResult result) {
    const auto operationId = std::get<OperationId>(result);
    return std::get<OperationSnapshot>(manager.snapshot(operationId));
}

inline OperationSnapshot awaitTerminal(
    OperationManager& manager,
    const OperationSnapshot& submitted,
    std::function<void()> atCompletion = [] {}) {
    std::promise<void> completed;
    auto future = completed.get_future();
    auto fence = manager.captureFence(submitted.sessionId);
    auto subscription = manager.subscribe(
        std::move(fence),
        [&] {
            atCompletion();
            completed.set_value();
        });
    if (future.wait_for(2s) != std::future_status::ready) {
        ADD_FAILURE() << "operation did not reach a terminal state";
    }
    return std::get<OperationSnapshot>(manager.snapshot(submitted.id));
}

}  // namespace vna::application::test_support
