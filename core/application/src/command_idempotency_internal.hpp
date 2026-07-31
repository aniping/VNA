#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include <vna/application/command_bus.hpp>

namespace vna::application {

class CommandBus::IdempotencyStore {
public:
    struct Lookup {
        bool keyFound;
        const CommandResult* replay;
    };

    explicit IdempotencyStore(std::size_t capacity);

    [[nodiscard]] Lookup lookup(
        const CommandEnvelope& command) const;
    void remember(
        const CommandEnvelope& command,
        const CommandResult& result);
    [[nodiscard]] CommandBusStats stats() const noexcept;

private:
    struct Entry {
        CommandEnvelope command;
        CommandResult result;
    };

    std::size_t capacity_;
    std::deque<Entry> entries_;
    std::uint64_t evictions_{0};
};

}  // namespace vna::application
