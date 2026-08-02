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

// name, message, status, and errorCode are UTF-8 values from stable caller
// catalogs. message is one sentence: never a body, token, sample array, or
// undeclared file content, and it must not contain control characters.
struct LogEvent {
    LogLevel level;
    std::string name;
    std::string message;
    std::optional<std::string> commandId;
    std::optional<std::string> sessionId;
    std::optional<std::string> instrumentId;
    std::optional<std::uint64_t> stateRevision;
    std::optional<std::string> status;
    std::optional<std::string> errorCode;
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
