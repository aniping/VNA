#pragma once

#include "vna/board/board_port.hpp"

#include <array>
#include <cstdint>

namespace vna::board {

using VirtualDuration = std::uint64_t;

enum class MockPrepareBehavior {
    Succeed,
    Reject
};

enum class MockRunBehavior {
    Succeed,
    Reject
};

struct MockCapabilityProfile final {
    std::uint32_t maximum_points{201U};
};

struct MockScenario final {
    MockPrepareBehavior prepare_behavior{MockPrepareBehavior::Succeed};
    VirtualDuration prepare_delay{1U};
    MockRunBehavior run_behavior{MockRunBehavior::Succeed};
    VirtualDuration run_delay{1U};
    std::uint32_t point_count{3U};
    std::array<ComplexSample, kMaximumContractChunkSamples> incident_a{};
    std::array<ComplexSample, kMaximumContractChunkSamples> response_b{};
    ChunkQuality incident_quality{};
    ChunkQuality response_quality{};
};

struct MockObservationSnapshot final {
    std::uint32_t accepted_prepare_calls{0U};
    std::uint32_t rejected_prepare_calls{0U};
    std::uint32_t prepare_terminal_callbacks{0U};
    std::uint32_t accepted_run_calls{0U};
    std::uint32_t rejected_run_calls{0U};
    std::uint32_t run_phase_callbacks{0U};
    std::uint32_t run_chunk_callbacks{0U};
    std::uint32_t run_terminal_callbacks{0U};
};

class MockBoardControl {
public:
    virtual ~MockBoardControl() = default;
    virtual void load_profile(MockCapabilityProfile profile) noexcept = 0;
    virtual void load_scenario(MockScenario scenario) noexcept = 0;
    virtual void advance(VirtualDuration delta) noexcept = 0;
    virtual MockObservationSnapshot observations() const noexcept = 0;
};

struct MockOpenedBoard final {
    OpenedBoard board;
    // Only valid while board owns the corresponding Mock session.
    MockBoardControl* control{nullptr};
};

class MockBoardProvider final : public BoardProvider {
public:
    MockBoardProvider(MockCapabilityProfile profile, MockScenario scenario) noexcept;

    core::Result<BoardInventorySnapshot, BoardError> discover(
        const BoardDiscoveryRequest& request) noexcept override;
    core::Result<OpenedBoard, BoardError> open(
        const BoardOpenRequest& request) noexcept override;
    core::Result<MockOpenedBoard, BoardError> open_controlled(
        const BoardOpenRequest& request) noexcept;

private:
    MockCapabilityProfile profile_{};
    MockScenario scenario_{};
};

}  // namespace vna::board
