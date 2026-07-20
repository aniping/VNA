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
    /// 接受请求但不按 run_duration 自动产生数据或 terminal，直到测试控制面释放。
    Stall,
    /// 以 Unsupported 同步拒绝并返还全部输入。
    Reject
};

/// 测试控制面为已接受 Stall Run 安排的迟到真实终态。
enum class MockStalledRunTerminal {
    /// 下一次 advance() 忽略尚未到期的 offset，交付已冻结成功计划并报告 terminal。
    /// @note 故障场景仍可让已冻结计划故意缺块或产生非合规 callback。
    Completed,
    /// 下一次 advance() 不交付数据并报告失败 terminal。
    Failed
};

/// Mock 模拟的底软 callback 源 Buffer 生命周期行为。
enum class MockDriverBufferBehavior {
    /// 测试不额外覆写源数组；Adapter 仍不得把它作为长期 payload 使用。
    NoExplicitReuse,
    /// 每次上层 on_chunk() 返回后立即用毒值覆盖源数组，模拟底软复用内存。
    ReuseImmediatelyAfterCallback
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

/// Mock 显式交付计划对单块 payload lease 的处理方式。
enum class MockChunkPayloadBehavior {
    /// 从首次派发前预留的 BufferPool 槽签发正常 move-only lease。
    ValidLease,
    /// 故意交付已经失效的 lease，用于验证 Ingress 拒绝不会静默丢块后成功。
    InvalidLease
};

/// Mock 在已接受 Run 中故意注入的一项非合规回调行为。
enum class MockRunContractFault {
    /// 不注入协议违约。
    None,
    /// 首个 chunk 携带错误 ManifestId。
    WrongManifest,
    /// 首个 chunk 携带错误 PreparedExecutionId。
    WrongPreparedExecution,
    /// 首个 chunk 携带错误 BoardRunId。
    WrongBoardRunId,
    /// 首个 chunk 携带错误 Run generation。
    WrongGeneration,
    /// 正常 terminal 返回后立即重复发送同一 terminal。
    MultipleTerminal,
    /// 正常 terminal 返回后继续交付一个带独立 lease 的 chunk。
    CallbackAfterTerminal
};

/// 测试可查询的 Mock 会话执行健康状态。
enum class MockSessionState {
    /// 会话仍允许新的 execution reservation。
    Healthy,
    /// Acquisition 已报告协议违约；本会话只允许关闭，不再接受执行。
    IsolatedContractViolation
};

/// Mock 在一次 Run 窗口内交付一个确定性数据块的计划项。
struct MockChunkDelivery final {
    /// 数据块对应的激励状态，必须匹配 Prepared Manifest。
    SourceStateId source_state{1U};
    /// 数据块对应的接收路径，必须匹配 Prepared Manifest。
    ReceiverPathId receiver_path{1U};
    /// 从 incident_a 或 response_b 选择数据源的原始波量身份。
    ReceiverWave wave{ReceiverWave::IncidentA};
    /// 本块第一个样本在完整观测中的零基点索引。显式故障剧本可在仍受
    /// kMaximumMockSweepPoints 约束时超出 Manifest 观测范围，以注入 OutOfRange。
    std::uint32_t point_begin{0U};
    /// 本块有效点数，范围为 [1, kMaximumContractChunkSamples]。
    std::uint32_t point_count{0U};
    /// 相对 Run 接受时刻的虚拟毫秒偏移，必须小于 run_duration。
    VirtualDuration offset{0U};
    /// 随本块传播到逐点 Quality Plane 的质量标志。
    ChunkQuality quality{};
    /// payload lease 是否有效；仅测试非合规 Ingress 交付路径时使用 InvalidLease。
    MockChunkPayloadBehavior payload_behavior{
        MockChunkPayloadBehavior::ValidLease};
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
    /// 底软源 Buffer 在上层 callback 返回后的模拟生命周期策略。
    MockDriverBufferBehavior driver_buffer_behavior{
        MockDriverBufferBehavior::NoExplicitReuse};
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
    /// 非 0 计划允许确定性表达 gap、重复、overlap 和 Manifest 范围错误。
    std::array<MockChunkDelivery, kMaximumRunChunks> chunk_deliveries{};
    /// chunk_deliveries 中有效计划项数量，范围为 [0, kMaximumRunChunks]。
    std::uint32_t chunk_delivery_count{0U};
    /// Succeed Run 在 Completed terminal 前最多交付的计划块数；0 表示交付全部，
    /// 非 0 值必须不大于解析后的计划数，可用于确定性制造 terminal-before-complete。
    std::uint32_t maximum_chunks_before_completed_terminal{0U};
    /// 对本次 Run 注入的唯一身份/终态协议故障；接受 Run 时按值冻结。
    MockRunContractFault contract_fault{MockRunContractFault::None};
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
    /// on_chunk() 返回后 Adapter 观察到 payload 已失效的累计块数。
    std::uint32_t consumed_chunk_payloads{0U};
    /// on_chunk() 返回后立即覆写底软源数组的累计次数。
    std::uint32_t reused_driver_buffers{0U};
    /// 因预留回退 Buffer 已耗尽而无法生成 lease 的累计块数。
    std::uint32_t failed_buffer_copies{0U};
    std::uint32_t run_terminal_callbacks{0U};
    /// Healthy 首次转入 IsolatedContractViolation 的累计次数；每个会话最多 1。
    std::uint32_t isolated_session_transitions{0U};
    /// 因会话已隔离而在新 Board Run 前拒绝的 execution reservation 累计数。
    std::uint32_t rejected_isolated_execution_reservations{0U};
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

    /// 为当前已接受且仍卡住的 Run 安排一个迟到 terminal。
    /// @param terminal 选择下一次 advance() 交付已冻结成功计划或失败 terminal。
    /// @return 当前确有一个 Stall Run 并已成功安排时返回 true；没有待办、剧本
    ///         不是 Stall 或已经安排过时返回 false。
    /// @note 本调用不内联触发 callback，不表示 abort、RF-off、readback 或安全
    ///       动作；它只模拟底软后来完成原 Run。
    virtual bool complete_stalled_run(
        MockStalledRunTerminal terminal) noexcept = 0;

    /// @return 当前契约事件累计计数的值快照。
    virtual MockObservationSnapshot observations() const noexcept = 0;

    /// @return 当前 Mock 会话执行健康状态的值快照。
    virtual MockSessionState session_state() const noexcept = 0;
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
    /// @return 成功时返回具有新非零 SessionId 的独占会话；不支持的请求或
    ///         分配失败时返回 BoardError。
    core::Result<OpenedBoard, BoardError> open(
        const BoardOpenRequest& request) noexcept override;

    /// 打开 Mock 会话并额外取得测试控制面。
    /// @param request 约束与 open() 相同。
    /// @return 成功时返回具有新非零 SessionId 的会话和非 owning 控制指针；
    ///         失败时返回 BoardError。
    core::Result<MockOpenedBoard, BoardError> open_controlled(
        const BoardOpenRequest& request) noexcept;

private:
    MockCapabilityProfile profile_{};
    MockScenario scenario_{};
    /// 为每次成功 open 分配不同的非零 BoardSessionId。
    std::uint64_t next_session_id_{1U};
};

}  // namespace vna::board
