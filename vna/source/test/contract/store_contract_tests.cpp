#include "test_support.h"

#include "runtime/resource/store/instrument_store.h"

#include <type_traits>
#include <utility>

namespace {

static_assert(
    !std::is_copy_constructible_v<vna::store::LifecycleTerminalReservation>);
static_assert(
    std::is_nothrow_move_constructible_v<vna::store::LifecycleTerminalReservation>);

TEST(InstrumentStoreContract, UncommittedLifecycleReservationReturnsCapacity) {
    using namespace vna::store;

    InstrumentStore store{1U};
    {
        auto first = store.reserve_lifecycle_terminal();
        VNA_REQUIRE(first.has_value());
        auto reservation = std::move(first).take_value();
        VNA_REQUIRE(reservation.valid());

        auto second = store.reserve_lifecycle_terminal();
        VNA_REQUIRE(!second.has_value());
        VNA_REQUIRE(second.error().code == StoreErrc::ResourceExhausted);
        VNA_REQUIRE(store.inspect().reserved_lifecycles == 1U);
    }

    VNA_REQUIRE(store.inspect().reserved_lifecycles == 0U);
    auto after_release = store.reserve_lifecycle_terminal();
    VNA_REQUIRE(after_release.has_value());
}

TEST(InstrumentStoreContract, RejectedInitialCommitCreatesNoVisibleLifecycle) {
    using namespace vna::store;

    InstrumentStore store{1U};
    {
        auto reservation_result = store.reserve_lifecycle_terminal();
        VNA_REQUIRE(reservation_result.has_value());
        auto reservation = std::move(reservation_result).take_value();
        auto commit = store.commit_accepted(
            OperationId{}, std::move(reservation));

        VNA_REQUIRE(std::holds_alternative<RejectedAcceptedCommit>(commit));
        const auto& rejected = std::get<RejectedAcceptedCommit>(commit);
        VNA_REQUIRE(rejected.error.code == StoreErrc::InvalidOperation);
        VNA_REQUIRE(rejected.reclaimed.valid());
        VNA_REQUIRE(store.inspect().visible_operations == 0U);
        VNA_REQUIRE(store.inspect().reserved_lifecycles == 1U);
    }

    VNA_REQUIRE(store.inspect().visible_operations == 0U);
    VNA_REQUIRE(store.inspect().reserved_lifecycles == 0U);
}

TEST(InstrumentStoreContract, CorrelatedAcceptedCommitRejectsMissingIdentity) {
    using namespace vna;

    store::InstrumentStore store{1U};
    {
        auto reservation_result = store.reserve_lifecycle_terminal();
        VNA_REQUIRE(reservation_result.has_value());
        auto commit = store.commit_accepted(
            store::OperationId{31U},
            runtime::WorkId{},
            core::StrongDigest{0x31U},
            std::move(reservation_result).take_value());

        VNA_REQUIRE(std::holds_alternative<store::RejectedAcceptedCommit>(commit));
        const auto& rejected = std::get<store::RejectedAcceptedCommit>(commit);
        VNA_REQUIRE(rejected.error.code == store::StoreErrc::InvalidOperation);
        VNA_REQUIRE(rejected.reclaimed.valid());
        VNA_REQUIRE(store.inspect().visible_operations == 0U);
        VNA_REQUIRE(store.inspect().revision == 0U);
    }

    auto digest_reservation = store.reserve_lifecycle_terminal();
    VNA_REQUIRE(digest_reservation.has_value());
    auto digest_commit = store.commit_accepted(
        store::OperationId{32U},
        runtime::WorkId{32U},
        core::StrongDigest{},
        std::move(digest_reservation).take_value());
    VNA_REQUIRE(
        std::holds_alternative<store::RejectedAcceptedCommit>(digest_commit));
    VNA_REQUIRE(
        std::get<store::RejectedAcceptedCommit>(digest_commit).error.code ==
        store::StoreErrc::InvalidOperation);
    VNA_REQUIRE(store.inspect().visible_operations == 0U);
    VNA_REQUIRE(store.inspect().revision == 0U);
}

TEST(InstrumentStoreContract, InstalledTerminalReservationCommitsUnderCapacityPressure) {
    using namespace vna::store;

    InstrumentStore store{2U};
    auto first_reservation_result = store.reserve_lifecycle_terminal();
    VNA_REQUIRE(first_reservation_result.has_value());
    auto first_reservation = std::move(first_reservation_result).take_value();
    auto accepted = store.commit_accepted(
        OperationId{41U}, std::move(first_reservation));
    VNA_REQUIRE(std::holds_alternative<AcceptedCommitReceipt>(accepted));
    VNA_REQUIRE(store.inspect_operation(OperationId{41U})->state == OperationState::Accepted);

    auto pressure = store.reserve_lifecycle_terminal();
    VNA_REQUIRE(pressure.has_value());
    VNA_REQUIRE(store.inspect().visible_operations == 1U);
    VNA_REQUIRE(store.inspect().reserved_lifecycles == 1U);
    auto no_more_capacity = store.reserve_lifecycle_terminal();
    VNA_REQUIRE(!no_more_capacity.has_value());

    auto terminal = store.commit_terminal(OperationId{41U}, OperationState::Failed);
    VNA_REQUIRE(terminal.has_value());
    VNA_REQUIRE(
        terminal.value().disposition == TerminalCommitDisposition::Committed);
    VNA_REQUIRE(store.inspect_operation(OperationId{41U})->state == OperationState::Failed);

    auto repeated = store.commit_terminal(OperationId{41U}, OperationState::Completed);
    VNA_REQUIRE(repeated.has_value());
    VNA_REQUIRE(
        repeated.value().disposition == TerminalCommitDisposition::AlreadyTerminal);
    VNA_REQUIRE(store.inspect_operation(OperationId{41U})->state == OperationState::Failed);
}

}  // namespace
