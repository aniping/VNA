#pragma once

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

class Logger {
public:
    virtual ~Logger() = default;

    // Synchronous implementations return only after every configured sink was
    // attempted. false reports encoding, size, or terminal sink failure.
    [[nodiscard]] virtual bool write(LogEvent event) noexcept = 0;
    // Flushes C++/library buffers, not the operating system's durable storage.
    [[nodiscard]] virtual bool flush() noexcept = 0;
};

}  // namespace vna::observability
