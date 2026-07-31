#pragma once

#include <cstdint>
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
    explicit TextId(std::string value) : value_(std::move(value)) {
        if (value_.empty() || value_.size() > 128) {
            throw std::invalid_argument{"TextId must be 1..128 bytes"};
        }
        for (const unsigned char byte : value_) {
            if (byte <= 0x1F || byte == 0x7F) {
                throw std::invalid_argument{
                    "TextId must not contain ASCII control bytes"};
            }
        }
    }

    TextId(const TextId&) = default;
    TextId& operator=(const TextId&) = default;

    [[nodiscard]] const std::string& value() const noexcept {
        return value_;
    }

    friend bool operator==(const TextId&, const TextId&) = default;

private:
    std::string value_;
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
