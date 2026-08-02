#include "human_log_formatter.hpp"

#include <spdlog/formatter.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/fmt/bundled/base.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vna::logging {
namespace {

std::tm localTime(std::time_t timestamp) {
    std::tm value{};
#ifdef _WIN32
    const bool failed = localtime_s(&value, &timestamp) != 0;
#else
    const bool failed = localtime_r(&timestamp, &value) == nullptr;
#endif
    if (failed) throw std::runtime_error("failed to convert local timestamp");
    return value;
}

std::time_t localFieldsAsUtc(std::tm value) {
    // Reinterpreting local calendar fields as UTC gives the offset without
    // relying on platform-specific timezone globals in the public interface.
#ifdef _WIN32
    return _mkgmtime64(&value);
#else
    return timegm(&value);
#endif
}

std::string localTimestamp(std::chrono::system_clock::time_point timestamp) {
    const auto epoch = std::chrono::system_clock::to_time_t(timestamp);
    const auto local = localTime(epoch);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;
    const auto offset = static_cast<long long>(localFieldsAsUtc(local) - epoch);
    const auto absolute = offset < 0 ? -offset : offset;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::put_time(&local, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << milliseconds.count()
           << (offset < 0 ? '-' : '+') << std::setw(2) << absolute / 3600
           << ':' << std::setw(2) << (absolute % 3600) / 60;
    return output.str();
}

void appendContext(
    std::string& output,
    const nlohmann::json& record,
    std::string_view key) {
    const auto value = record.find(key);
    if (value == record.end()) return;
    output += " ";
    output += key;
    output += '=';
    output += value->is_string() ? value->get<std::string>() : value->dump();
}

class HumanLogFormatter final : public spdlog::formatter {
public:
    void format(
        const spdlog::details::log_msg& message,
        spdlog::memory_buf_t& destination) override {
        // The already encoded record is the single field truth; this formatter
        // only presents it and therefore owns no server or Web event catalog.
        const auto record = nlohmann::json::parse(
            message.payload.begin(), message.payload.end());
        auto output = localTimestamp(message.time) + " [" +
            record.at("level").get<std::string>() + "] " +
            record.at("message").get<std::string>();
        for (const auto key : {"command_id", "session_id", "instrument_id",
                               "state_revision", "error_code"}) {
            appendContext(output, record, key);
        }
        output += '\n';
        destination.append(output.data(), output.data() + output.size());
    }

    std::unique_ptr<spdlog::formatter> clone() const override {
        return std::make_unique<HumanLogFormatter>();
    }
};

}  // namespace

std::unique_ptr<spdlog::formatter> makeHumanLogFormatter() {
    return std::make_unique<HumanLogFormatter>();
}

}  // namespace vna::logging
