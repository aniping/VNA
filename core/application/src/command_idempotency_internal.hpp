#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <vna/application/command_bus.hpp>

namespace vna::application {

class CommandBus::IdempotencyStore {
public:
    struct Lookup {
        bool keyFound;
        const CommandResult* replay;
    };

    struct Prepared {
        std::shared_ptr<const CommandEnvelope> command;
    };

    explicit IdempotencyStore(std::size_t capacity);

    [[nodiscard]] Lookup lookup(
        const CommandEnvelope& command) const;
    [[nodiscard]] static bool isCacheable(
        const CommandResult& result) noexcept;
    [[nodiscard]] Prepared prepare(const CommandEnvelope& command) const;
    void commit(Prepared prepared, CommandResult result) noexcept;
    [[nodiscard]] CommandBusStats stats() const noexcept;

private:
    struct Entry {
        Entry(
            std::shared_ptr<const CommandEnvelope> preparedCommand,
            CommandResult preparedResult) noexcept
            : command(std::move(preparedCommand)),
              result(std::move(preparedResult)) {}

        std::shared_ptr<const CommandEnvelope> command;
        CommandResult result;
    };

    std::vector<std::optional<Entry>> entries_;
    std::size_t size_{0};
    std::size_t next_{0};
    std::uint64_t evictions_{0};
};

}  // namespace vna::application
