#include "command_idempotency_internal.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace vna::application {
namespace {

bool sameDouble(double left, double right) noexcept {
    using DoubleBits = std::array<std::byte, sizeof(double)>;
    const auto bits = [](double value) noexcept {
#if defined(__cpp_lib_bit_cast)
        return std::bit_cast<DoubleBits>(value);
#else
        DoubleBits result{};
        std::memcpy(result.data(), &value, sizeof(value));
        return result;
#endif
    };
    return bits(left) == bits(right);
}

bool sameSweep(
    const domain::SweepSettings& left,
    const domain::SweepSettings& right) noexcept {
    return left.startFrequencyHz == right.startFrequencyHz &&
        left.stopFrequencyHz == right.stopFrequencyHz &&
        left.points == right.points &&
        left.ifBandwidthHz == right.ifBandwidthHz &&
        sameDouble(left.powerDbm, right.powerDbm);
}

bool samePayload(
    const CreateChannelCommand& left,
    const CreateChannelCommand& right) noexcept {
    return sameSweep(left.sweep, right.sweep);
}

bool samePayload(
    const UpdateChannelSweepCommand& left,
    const UpdateChannelSweepCommand& right) noexcept {
    return left.channelId == right.channelId && sameSweep(left.sweep, right.sweep);
}

bool samePayload(
    const CreateMeasurementCommand& left,
    const CreateMeasurementCommand& right) noexcept {
    return left.channelId == right.channelId && left.type == right.type;
}

bool samePayload(
    const CreateWindowCommand&,
    const CreateWindowCommand&) noexcept {
    return true;
}

bool samePayload(
    const CreateTraceCommand& left,
    const CreateTraceCommand& right) noexcept {
    return left.windowId == right.windowId &&
        left.measurementId == right.measurementId && left.format == right.format;
}

bool samePayload(
    const UpdateTraceFormatCommand& left,
    const UpdateTraceFormatCommand& right) noexcept {
    return left.traceId == right.traceId && left.format == right.format;
}

bool samePayload(
    const UpdateTraceScalePerDivisionCommand& left,
    const UpdateTraceScalePerDivisionCommand& right) noexcept {
    return left.traceId == right.traceId &&
        sameDouble(left.scalePerDivision, right.scalePerDivision);
}

bool samePayload(
    const RemoveTraceCommand& left,
    const RemoveTraceCommand& right) noexcept {
    return left.traceId == right.traceId;
}

bool samePayload(
    const StartSingleSweepCommand& left,
    const StartSingleSweepCommand& right) noexcept {
    return left.channelId == right.channelId;
}

bool sameSignature(
    const CommandEnvelope& left,
    const CommandEnvelope& right) noexcept {
    if (left.origin != right.origin ||
        left.expectedStateRevision != right.expectedStateRevision) {
        return false;
    }
    return std::visit(
        [](const auto& leftPayload, const auto& rightPayload) {
            using Left = std::decay_t<decltype(leftPayload)>;
            using Right = std::decay_t<decltype(rightPayload)>;
            if constexpr (std::is_same_v<Left, Right>) {
                return samePayload(leftPayload, rightPayload);
            }
            return false;
        },
        left.payload,
        right.payload);
}

bool sameKey(
    const CommandEnvelope& left,
    const CommandEnvelope& right) noexcept {
    return left.instrumentId == right.instrumentId &&
        left.sessionId == right.sessionId && left.commandId == right.commandId;
}

}  // namespace

CommandBus::IdempotencyStore::IdempotencyStore(std::size_t capacity)
    : capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument{"idempotency capacity must be positive"};
    }
}

CommandBus::IdempotencyStore::Lookup CommandBus::IdempotencyStore::lookup(
    const CommandEnvelope& command) const {
    for (const auto& entry : entries_) {
        if (sameKey(entry.command, command)) {
            return {
                .keyFound = true,
                .replay = sameSignature(entry.command, command)
                    ? &entry.result
                    : nullptr,
            };
        }
    }
    return {.keyFound = false, .replay = nullptr};
}

void CommandBus::IdempotencyStore::remember(
    const CommandEnvelope& command,
    const CommandResult& result) {
    if (entries_.size() == capacity_) {
        entries_.pop_front();
        ++evictions_;
    }
    entries_.push_back(Entry{command, result});
}

CommandBusStats CommandBus::IdempotencyStore::stats() const noexcept {
    return {
        .idempotencyEntries = entries_.size(),
        .idempotencyEvictions = evictions_,
    };
}

}  // namespace vna::application
