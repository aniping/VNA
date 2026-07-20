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

TEST(InstrumentStoreContract,
     DrainTerminalRejectsWrongIdentityAndDuplicateWithTypedErrors) {
    using namespace vna;

    store::InstrumentStore store{1U};
    auto reservation = store.reserve_lifecycle_terminal();
    VNA_REQUIRE(reservation.has_value());
    const store::OperationId operation{41U};
    const runtime::DrainId drain{73U};
    const auto accepted = store.commit_accepted(
        operation,
        runtime::WorkId{51U},
        core::StrongDigest{61U},
        std::move(reservation).take_value());
    VNA_REQUIRE(std::holds_alternative<store::AcceptedCommitReceipt>(accepted));

    acquisition::AcquisitionDrainOwnershipSnapshot ownership{};
    ownership.board_run_callback_obligation = true;
    ownership.manifest_owned = true;
    ownership.builder_owned = true;
    ownership.buffer_ingress_owned = true;
    ownership.runtime_completion_registered = true;
    ownership.a_only_completion_owned = true;
    ownership.disabled_preview_owned = true;
    ownership.exact_finalization_consumed = true;
    ownership.run_resources_narrowed = true;
    const auto handoff = store.commit_acquisition_draining(
        operation,
        drain,
        acquisition::AcquisitionFailure{},
        ownership);
    VNA_REQUIRE(handoff.has_value());

    const auto wrong_identity = store.commit_drain_terminal(
        operation,
        runtime::DrainId{74U},
        runtime::RuntimeDrainTerminalKind::Drained);
    VNA_REQUIRE(!wrong_identity.has_value());
    VNA_REQUIRE(
        wrong_identity.error().code == store::StoreErrc::DrainIdentityMismatch);
    VNA_REQUIRE(
        store.inspect_drain(drain)->state == store::DrainState::Draining);

    const auto committed = store.commit_drain_terminal(
        operation, drain, runtime::RuntimeDrainTerminalKind::Drained);
    VNA_REQUIRE(committed.has_value());
    VNA_REQUIRE(committed.value().drain == drain);
    VNA_REQUIRE(committed.value().state == store::DrainState::Drained);

    const auto duplicate = store.commit_drain_terminal(
        operation, drain, runtime::RuntimeDrainTerminalKind::Drained);
    VNA_REQUIRE(!duplicate.has_value());
    VNA_REQUIRE(
        duplicate.error().code == store::StoreErrc::DrainAlreadyTerminal);
    VNA_REQUIRE(store.inspect().drain_events == 1U);
}

}  // namespace
