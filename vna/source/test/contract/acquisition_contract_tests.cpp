#include "test_support.h"

#include "runtime/function/acquisition/acquisition_admission.h"

namespace {

TEST(AcquisitionAdmissionContract, ManifestCanOnlyNarrowFrozenEnvelopeOnce) {
    using namespace vna;

    const core::StrongDigest plan_digest{0xA011U};
    const board::CapabilitySnapshot capabilities{
        board::BoardContractVersion{1U, 0U},
        board::BoardSessionId{11U},
        1U,
        2U,
        3U,
        4U,
        core::StrongDigest{0xCA9U},
        201U};
    acquisition::AcquisitionAdmissionPool pool{2U};
    const acquisition::AcquisitionAdmissionPool::Claim claim{
        plan_digest,
        capabilities,
        201U,
        2U,
        1.0e6,
        201.0e6};

    const board::PreparedExecutionManifest expanded{
        board::ManifestId{21U},
        board::PreparedExecutionId{22U},
        capabilities.session_id,
        capabilities.session_epoch,
        capabilities.capability_revision,
        capabilities.topology_epoch,
        capabilities.operational_epoch,
        plan_digest,
        core::StrongDigest{0xAAU},
        201U,
        1.0e6,
        202.0e6};
    auto narrowed = expanded;
    narrowed.actual_point_count = 101U;
    narrowed.actual_start_hz = 2.0e6;
    narrowed.actual_stop_hz = 200.0e6;

    {
        auto invalid_reserved = pool.reserve(claim);
        VNA_REQUIRE(invalid_reserved.has_value());
        auto invalid_lease = std::move(invalid_reserved).take_value();
        VNA_REQUIRE(invalid_lease.owns_pre_dispatch_resources());
        VNA_REQUIRE(!invalid_lease.narrow_to(expanded));
        VNA_REQUIRE(!invalid_lease.narrow_to(narrowed));
        VNA_REQUIRE(invalid_lease.finalize_failure());
        VNA_REQUIRE(!invalid_lease.finalize_failure());

        auto valid_reserved = pool.reserve(claim);
        VNA_REQUIRE(valid_reserved.has_value());
        auto valid_lease = std::move(valid_reserved).take_value();
        VNA_REQUIRE(valid_lease.narrow_to(narrowed));
        VNA_REQUIRE(!valid_lease.narrow_to(narrowed));
        VNA_REQUIRE(valid_lease.finalize_failure());
        VNA_REQUIRE(!valid_lease.finalize_failure());
        VNA_REQUIRE(pool.inspect().in_use == 2U);
    }
    VNA_REQUIRE(pool.inspect().in_use == 0U);
    VNA_REQUIRE(pool.inspect().failure_finalizations == 2U);
}

}  // namespace
