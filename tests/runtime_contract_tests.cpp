#include "test_support.hpp"

#include "vna/runtime/operation_runtime.hpp"

#include <type_traits>
#include <utility>

namespace {

static_assert(!std::is_copy_constructible_v<vna::runtime::ReservedWorkDispatch>);
static_assert(std::is_nothrow_move_constructible_v<vna::runtime::ReservedWorkDispatch>);

class CompletingWork final : public vna::runtime::RuntimeWork {
public:
    vna::runtime::RuntimeTerminal execute() noexcept override {
        ++executions;
        return vna::runtime::RuntimeTerminal{
            vna::runtime::RuntimeTerminalKind::Completed};
    }

    std::uint32_t executions{0U};
};

class RecordingCompletionSink final : public vna::runtime::RuntimeCompletionSink {
public:
    void on_runtime_terminal(
        vna::runtime::WorkId value,
        vna::runtime::RuntimeTerminal result) noexcept override {
        ++terminals;
        work = value;
        terminal = result;
    }

    std::uint32_t terminals{0U};
    vna::runtime::WorkId work{};
    vna::runtime::RuntimeTerminal terminal{};
};

void undispatched_reservation_returns_fixed_capacity() {
    using namespace vna::runtime;

    OperationRuntime runtime{1U};
    {
        auto first = runtime.reserve_work(WorkId{1U});
        VNA_REQUIRE(first.has_value());
        auto reservation = std::move(first).take_value();
        VNA_REQUIRE(reservation.valid());

        auto second = runtime.reserve_work(WorkId{2U});
        VNA_REQUIRE(!second.has_value());
        VNA_REQUIRE(second.error().code == RuntimeErrc::ResourceExhausted);
        VNA_REQUIRE(runtime.inspect().reserved == 1U);
    }

    VNA_REQUIRE(runtime.inspect().reserved == 0U);
    auto after_release = runtime.reserve_work(WorkId{3U});
    VNA_REQUIRE(after_release.has_value());
}

void reserved_dispatch_is_non_inline_and_completes_once() {
    using namespace vna::runtime;

    OperationRuntime runtime{1U};
    auto reservation_result = runtime.reserve_work(WorkId{11U});
    VNA_REQUIRE(reservation_result.has_value());
    auto reservation = std::move(reservation_result).take_value();
    CompletingWork work;
    RecordingCompletionSink completion;

    auto dispatch = runtime.dispatch(
        std::move(reservation),
        work,
        RuntimeCompletionRegistration{completion});
    VNA_REQUIRE(dispatch.has_value());
    VNA_REQUIRE(work.executions == 0U);
    VNA_REQUIRE(completion.terminals == 0U);
    VNA_REQUIRE(runtime.inspect().queued == 1U);

    VNA_REQUIRE(runtime.run_one());
    VNA_REQUIRE(work.executions == 1U);
    VNA_REQUIRE(completion.terminals == 1U);
    VNA_REQUIRE(completion.work == WorkId{11U});
    VNA_REQUIRE(completion.terminal.kind == RuntimeTerminalKind::Completed);
    VNA_REQUIRE(runtime.inspect().queued == 0U);
    VNA_REQUIRE(runtime.inspect().completed == 1U);

    VNA_REQUIRE(!runtime.run_one());
    VNA_REQUIRE(completion.terminals == 1U);
}

}  // namespace

int main() {
    int failures = 0;
    failures += vna::test::run(
        "undispatched reservation returns fixed capacity",
        undispatched_reservation_returns_fixed_capacity);
    failures += vna::test::run(
        "reserved dispatch is non-inline and completes once",
        reserved_dispatch_is_non_inline_and_completes_once);
    return failures;
}
