#include "json_line_formatter.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace vna::logging {
namespace {

std::string_view levelName(observability::LogLevel level) {
    using observability::LogLevel;
    switch (level) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error: return "error";
    }
    throw std::invalid_argument("unknown log level");
}

std::tm utcTime(std::time_t timestamp) {
    std::tm value{};
#ifdef _WIN32
    const bool failed = gmtime_s(&value, &timestamp) != 0;
#else
    const bool failed = gmtime_r(&timestamp, &value) == nullptr;
#endif
    if (failed) {
        throw std::runtime_error("failed to convert UTC timestamp");
    }
    return value;
}

std::string formatTimestamp(std::chrono::system_clock::time_point timestamp) {
    const auto time = std::chrono::system_clock::to_time_t(timestamp);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()) %
        1000;
    const auto utc = utcTime(time);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << milliseconds.count() << 'Z';
    return output.str();
}

}  // namespace

std::string formatJsonLine(
    const observability::LogEvent& event,
    std::chrono::system_clock::time_point timestamp) {
    nlohmann::json record{
        {"timestamp", formatTimestamp(timestamp)},
        {"level", levelName(event.level)},
        {"event", event.name},
    };
    if (event.commandId) record["command_id"] = *event.commandId;
    if (event.sessionId) record["session_id"] = *event.sessionId;
    if (event.instrumentId) record["instrument_id"] = *event.instrumentId;
    if (event.stateRevision) record["state_revision"] = *event.stateRevision;
    if (event.status) record["status"] = *event.status;
    return record.dump() + '\n';
}

}  // namespace vna::logging
