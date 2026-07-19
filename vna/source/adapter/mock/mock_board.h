#pragma once

#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstdint>

namespace vna::board {

/// Mock 单板使用的离散虚拟毫秒；不读取 wall clock，也不调用 sleep。
using VirtualDuration = std::uint64_t;

/// Mock 波形源支持的最大实际点数，与当前 A-only 产品切片一致。
constexpr std::size_t kMaximumMockSweepPoints = 201U;

/// Mock 对 Prepare 请求的同步处理策略。
enum class MockPrepareBehavior {
    /// 接受请求，并在虚拟延时到期后回调成功终态。
    Succeed,
    /// 接受请求，并在虚拟延时到期后以 cleanup evidence 回调失败终态。
    Fail,
    /// 以 Unsupported 同步拒绝并返还全部输入。
    Reject
};

/// Mock 对 Run 请求的同步处理策略。
enum class MockRunBehavior {
    /// 接受请求，在运行窗口内按计划交付 A/B 数据，并在窗口末端报告成功终态。
    Succeed,
    /// 接受请求，在运行窗口末端报告失败终态，不交付数据块。
    Fail,
    /// 以 Unsupported 同步拒绝并返还全部输入。
    Reject
};

/// Mock Prepare 成功终态中实际 Manifest 的一致性故障剧本。
enum class MockManifestBehavior {
    /// 返回与接受时意图及能力 cut 完全匹配的正常 Manifest。
    MatchIntent,
    /// 返回高于授权 cut 的 capability revision，用于验证过期版本拒绝。
    StaleCapabilityRevision,
    /// 返回与授权 cut 不同的 BoardSessionId，用于验证身份不匹配拒绝。
    MismatchedSession,
    /// 返回大于请求保守点数 envelope 的实际点数，用于验证禁止扩容。
    ExpandedPointEnvelope
};

/// Mock 成功 Run 的正式观测交付策略。
enum class MockObservationBehavior {
    /// 按 Manifest 交付全部必需观测后报告 Completed。
    Complete,
    /// 省略响应波 b 但仍报告 Completed，用于证明 terminal 不等于完整覆盖。
    OmitResponseButComplete
};

/// Mock 对外公布的硬件能力配置。
struct MockCapabilityProfile final {
    /// 单次扫描支持的最大点数。
    std::uint32_t maximum_points{201U};
};

/// Mock 在一次 Run 窗口内交付一个确定性数据块的计划项。
struct MockChunkDelivery final {
    /// 数据块对应的激励状态，必须匹配 Prepared Manifest。
    SourceStateId source_state{1U};
    /// 数据块对应的接收路径，必须匹配 Prepared Manifest。
    ReceiverPathId receiver_path{1U};
    /// 从 incident_a 或 response_b 选择数据源的原始波量身份。
    ReceiverWave wave{ReceiverWave::IncidentA};
    /// 本块第一个样本在完整观测中的零基点索引。
    std::uint32_t point_begin{0U};
    /// 本块有效点数，范围为 [1, kMaximumContractChunkSamples]。
    std::uint32_t point_count{0U};
    /// 相对 Run 接受时刻的虚拟毫秒偏移，必须小于 run_duration。
    VirtualDuration offset{0U};
    /// 随本块传播到逐点 Quality Plane 的质量标志。
    ChunkQuality quality{};
};

/// 一次可重复 Mock 扫描的输入剧本。
struct MockScenario final {
    /// Prepare 是成功、异步失败还是同步拒绝；接受 Prepare 时按值冻结。
    MockPrepareBehavior prepare_behavior{MockPrepareBehavior::Succeed};
    /// Prepare 从接受到终态所需的虚拟毫秒数。
    VirtualDuration prepare_delay{1U};
    /// PrepareSucceeded 中 Manifest 的一致性行为；接受 Prepare 时按值冻结。
    MockManifestBehavior manifest_behavior{MockManifestBehavior::MatchIntent};
    /// Prepared discard 从接受到唯一 cleanup terminal 的虚拟毫秒数。
    VirtualDuration discard_delay{1U};
    /// Run 是成功、异步失败还是同步拒绝；接受 Run 时按值冻结。
    MockRunBehavior run_behavior{MockRunBehavior::Succeed};
    /// Run 从接受到唯一终态的虚拟毫秒数；有效范围为 [300, 400]。
    VirtualDuration run_duration{350U};
    /// 成功 Run 如何交付 Manifest 要求的观测；接受 Run 时按值冻结。
    MockObservationBehavior observation_behavior{
        MockObservationBehavior::Complete};
    /// 将作为 IncidentA 数据块交付的确定性复数样本。
    std::array<ComplexSample, kMaximumMockSweepPoints> incident_a{};
    /// 将作为 ResponseB 数据块交付的确定性复数样本。
    std::array<ComplexSample, kMaximumMockSweepPoints> response_b{};
    /// A 波数据块携带的质量标记。
    ChunkQuality incident_quality{};
    /// B 波数据块携带的质量标记。
    ChunkQuality response_quality{};
    /// 可选的显式交付计划；count 为 0 时由 Manifest 确定性生成完整分块。
    std::array<MockChunkDelivery, kMaximumRunChunks> chunk_deliveries{};
    /// chunk_deliveries 中有效计划项数量，范围为 [0, kMaximumRunChunks]。
    std::uint32_t chunk_delivery_count{0U};
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
    /// Adapter 接受并异步闭合的 Prepared discard 累计数。
    std::uint32_t accepted_discard_calls{0U};
    /// 因身份/phase 非法而同步拒绝的 Prepared discard 累计数。
    std::uint32_t rejected_discard_calls{0U};
    /// 已交付的 Prepared discard 唯一 terminal 累计数。
    std::uint32_t discard_terminal_callbacks{0U};
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
    /// @param scenario 新剧本；控制器保存其副本，已接受的
    ///        Prepare/Run/Prepared-discard 保留接受时捕获的旧剧本与 delay。
    virtual void load_scenario(MockScenario scenario) noexcept = 0;

    /// 推进虚拟时间并同步触发所有到期的 Prepare/Run/Prepared-discard 回调。
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
