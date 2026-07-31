#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <vna/application/operation_id.hpp>
#include <vna/display_model/display_workspace.hpp>
#include <vna/domain/instrument.hpp>

namespace vna::application {

// Protocol adapters construct this contract, while CommandBus owns execution.
// Keeping the value types separate prevents the bus implementation interface
// from becoming the dependency required by every command producer.
template <typename Tag>
class TextId {
public:
    explicit TextId(std::string value) {
        if (value.empty() || value.size() > 128) {
            throw std::invalid_argument{"TextId must be 1..128 bytes"};
        }
        for (const unsigned char byte : value) {
            if (byte <= 0x1F || byte == 0x7F) {
                throw std::invalid_argument{
                    "TextId must not contain ASCII control bytes"};
            }
        }
        value_ = std::make_shared<const std::string>(std::move(value));
    }

    // IDs cross admission and idempotency commit boundaries. Sharing immutable
    // text makes every established ID cheap and non-throwing to copy, while a
    // move deliberately shares ownership so the source remains a valid ID.
    TextId(const TextId&) noexcept = default;
    TextId& operator=(const TextId&) noexcept = default;
    TextId(TextId&& other) noexcept : value_(other.value_) {}
    TextId& operator=(TextId&& other) noexcept {
        value_ = other.value_;
        return *this;
    }

    [[nodiscard]] const std::string& value() const noexcept {
        return *value_;
    }

    friend bool operator==(const TextId& left, const TextId& right) noexcept {
        return left.value_ == right.value_ || *left.value_ == *right.value_;
    }

private:
    std::shared_ptr<const std::string> value_;
};

using CommandId = TextId<struct CommandIdTag>;
using SessionId = TextId<struct SessionIdTag>;
using InstrumentId = TextId<struct InstrumentIdTag>;

struct CreateChannelCommand {
    domain::SweepSettings sweep;
};

struct UpdateChannelSweepCommand {
    domain::ChannelId channelId;
    domain::SweepSettings sweep;
};

struct CreateMeasurementCommand {
    domain::ChannelId channelId;
    domain::MeasurementType type;
};

struct CreateWindowCommand {};

struct CreateTraceCommand {
    display_model::WindowId windowId;
    domain::MeasurementId measurementId;
    display_model::TraceFormat format;
};

struct UpdateTraceFormatCommand {
    display_model::TraceId traceId;
    display_model::TraceFormat format;
};

struct UpdateTraceScalePerDivisionCommand {
    display_model::TraceId traceId;
    double scalePerDivision;
};

struct RemoveTraceCommand {
    display_model::TraceId traceId;
};

struct StartSingleSweepCommand {
    domain::ChannelId channelId;
};

using CommandPayload = std::variant<
    CreateChannelCommand,
    UpdateChannelSweepCommand,
    CreateMeasurementCommand,
    CreateWindowCommand,
    CreateTraceCommand,
    UpdateTraceFormatCommand,
    UpdateTraceScalePerDivisionCommand,
    RemoveTraceCommand,
    StartSingleSweepCommand>;

using CommandValue = std::variant<
    std::monostate,
    domain::ChannelId,
    domain::MeasurementId,
    display_model::WindowId,
    display_model::TraceId,
    OperationId>;

enum class CommandOrigin {
    Web,
    Scpi,
};

struct CommandEnvelope {
    CommandId commandId;
    SessionId sessionId;
    InstrumentId instrumentId;
    CommandOrigin origin;
    std::optional<std::uint64_t> expectedStateRevision;
    CommandPayload payload;
};

}  // namespace vna::application
