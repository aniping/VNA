#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace vna::observability {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

// name and status are UTF-8 values produced by stable caller catalogs.
struct LogEvent {
    LogLevel level;
    std::string name;
    std::optional<std::string> commandId;
    std::optional<std::string> sessionId;
    std::optional<std::string> instrumentId;
    std::optional<std::uint64_t> stateRevision;
    std::optional<std::string> status;
};

enum class SubmitResult {
    Accepted,
    // The logger stopped accepting events after a terminal sink failure.
    Stopped,
};

struct LoggerStatistics {
    std::uint64_t sinkFailures{0};
};

class Logger {
public:
    virtual ~Logger() = default;

    [[nodiscard]] virtual SubmitResult submit(LogEvent event) = 0;
    // A stream-flush barrier for events Accepted before this call, not fsync.
    // A timeout returns false without forgetting pending events.
    [[nodiscard]] virtual bool flush(
        std::chrono::milliseconds timeout) = 0;
    [[nodiscard]] virtual LoggerStatistics statistics()
        const noexcept = 0;
};

}  // namespace vna::observability
