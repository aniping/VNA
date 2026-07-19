#pragma once

#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstdint>

namespace vna::board {

/// Mock 单板使用的离散虚拟毫秒；不读取 wall clock，也不调用 sleep。
using VirtualDuration = std::uint64_t;

/// Mock 对 Prepare 请求的同步处理策略。
enum class MockPrepareBehavior {
    /// 接受请求，并在虚拟延时到期后回调成功终态。
    Succeed,
    /// 以 Unsupported 同步拒绝并返还全部输入。
    Reject
};

/// Mock 对 Run 请求的同步处理策略。
enum class MockRunBehavior {
    /// 接受请求，并在虚拟延时到期后交付阶段、A/B 数据和终态。
    Succeed,
    /// 接受请求，并在虚拟延时到期后交付阶段和失败终态，不交付数据块。
    Fail,
    /// 以 Unsupported 同步拒绝并返还全部输入。
    Reject
};

/// Mock 对外公布的硬件能力配置。
struct MockCapabilityProfile final {
    /// 单次扫描支持的最大点数。
    std::uint32_t maximum_points{201U};
};

/// 一次可重复 Mock 扫描的输入剧本。
struct MockScenario final {
    MockPrepareBehavior prepare_behavior{MockPrepareBehavior::Succeed};
    /// Prepare 从接受到终态所需的虚拟毫秒数。
    VirtualDuration prepare_delay{1U};
    MockRunBehavior run_behavior{MockRunBehavior::Succeed};
    /// Run 从接受到开始交付事件所需的虚拟毫秒数。
    VirtualDuration run_delay{1U};
    /// 每种接收机波形交付的有效点数，最大为 kMaximumContractChunkSamples。
    std::uint32_t point_count{3U};
    /// 将作为 IncidentA 数据块交付的确定性复数样本。
    std::array<ComplexSample, kMaximumContractChunkSamples> incident_a{};
    /// 将作为 ResponseB 数据块交付的确定性复数样本。
    std::array<ComplexSample, kMaximumContractChunkSamples> response_b{};
    /// A 波数据块携带的质量标记。
    ChunkQuality incident_quality{};
    /// B 波数据块携带的质量标记。
    ChunkQuality response_quality{};
};

/// Mock 从创建以来发生的契约事件计数快照。
struct MockObservationSnapshot final {
    /// submit 准入阶段成功取得的实际 execution 槽累计数。
    std::uint32_t acquired_execution_reservations{0U};
    /// 因 execution 槽已被占用而拒绝的预留请求累计数。
    std::uint32_t rejected_execution_reservations{0U};
    /// RAII owner 终结后实际归还的 execution 槽累计数。
    std::uint32_t released_execution_reservations{0U};
    std::uint32_t accepted_prepare_calls{0U};
    std::uint32_t rejected_prepare_calls{0U};
    std::uint32_t prepare_terminal_callbacks{0U};
    std::uint32_t accepted_run_calls{0U};
    std::uint32_t rejected_run_calls{0U};
    std::uint32_t run_phase_callbacks{0U};
    std::uint32_t run_chunk_callbacks{0U};
    std::uint32_t run_terminal_callbacks{0U};
};

/// 测试侧控制已打开 Mock 会话的接口。
///
/// 控制接口不是生产 BoardPort 的一部分；它让测试无需休眠、线程或真实硬件，
/// 就能修改后续行为并确定性推进异步回调。
class MockBoardControl {
public:
    virtual ~MockBoardControl() = default;

    /// 替换后续能力查询使用的配置。
    /// @param profile 新能力配置；控制器保存其副本，不改变当前虚拟时间和事件计数。
    virtual void load_profile(MockCapabilityProfile profile) noexcept = 0;

    /// 替换后续请求捕获的场景。
    /// @param scenario 新剧本；控制器保存其副本，已接受的 Run 保留接受时捕获的旧剧本。
    virtual void load_scenario(MockScenario scenario) noexcept = 0;

    /// 推进虚拟时间并同步触发所有到期的 Prepare/Run 回调。
    /// @param delta 非负虚拟时间增量；0 也会处理已经到期的请求。
    virtual void advance(VirtualDuration delta) noexcept = 0;

    /// @return 当前契约事件累计计数的值快照。
    virtual MockObservationSnapshot observations() const noexcept = 0;
};

/// 同时返回生产会话句柄和测试控制面的打开结果。
struct MockOpenedBoard final {
    /// 独占拥有 MockBoardSession 的生产接口句柄。
    OpenedBoard board;
    /// 不拥有会话的测试控制指针；只在 board 仍持有对应 Mock 会话时有效。
    MockBoardControl* control{nullptr};
};

/// 创建确定性 Mock 单板会话的 Provider。
class MockBoardProvider final : public BoardProvider {
public:
    /// @param profile 新会话初始公布的能力；Provider 保存其副本。
    /// @param scenario 新会话初始执行的剧本；Provider 保存其副本。
    MockBoardProvider(MockCapabilityProfile profile, MockScenario scenario) noexcept;

    /// 发现固定 selector=1 的 Mock 单板。
    /// @param request maximum_entries 必须大于 0。
    /// @return 成功时返回一个条目的清单；请求非法时返回 InvalidIntent。
    core::Result<BoardInventorySnapshot, BoardError> discover(
        const BoardDiscoveryRequest& request) noexcept override;

    /// 按标准生产接口打开 Mock 会话。
    /// @param request selector 必须为 1，接受的主契约版本必须为 1。
    /// @return 成功时返回独占会话；不支持的请求或分配失败时返回 BoardError。
    core::Result<OpenedBoard, BoardError> open(
        const BoardOpenRequest& request) noexcept override;

    /// 打开 Mock 会话并额外取得测试控制面。
    /// @param request 约束与 open() 相同。
    /// @return 成功时返回会话和非 owning 控制指针；失败时返回 BoardError。
    core::Result<MockOpenedBoard, BoardError> open_controlled(
        const BoardOpenRequest& request) noexcept;

private:
    MockCapabilityProfile profile_{};
    MockScenario scenario_{};
};

}  // namespace vna::board
