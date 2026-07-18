#pragma once

#include "runtime/core/base/result.h"
#include "runtime/core/base/strong_id.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>

namespace vna::board {

/// 以下强类型 ID 分别标识单板会话、Prepare 调用、已准备执行、清单、
/// Run 调用、Run 代次和数据块序号；不同种类的 ID 不能隐式混用。
using BoardSessionId = core::StrongId<struct BoardSessionIdTag>;
using PrepareCallId = core::StrongId<struct PrepareCallIdTag>;
using PreparedExecutionId = core::StrongId<struct PreparedExecutionIdTag>;
using ManifestId = core::StrongId<struct ManifestIdTag>;
using BoardRunId = core::StrongId<struct BoardRunIdTag>;
using RunGeneration = core::StrongId<struct RunGenerationTag>;
using ChunkSequence = core::StrongId<struct ChunkSequenceTag>;

/// 上层和单板适配器共同支持的契约版本。
struct BoardContractVersion final {
    /// 不兼容修改递增主版本号。
    std::uint16_t major{1U};
    /// 向后兼容修改递增次版本号。
    std::uint16_t minor{0U};
};

/// BoardPort 调用失败的稳定错误分类。
enum class BoardErrc {
    /// 扫描点数、频率范围或摘要等意图字段非法。
    InvalidIntent,
    /// 单板、契约版本或所请求能力不受支持。
    Unsupported,
    /// 授权引用的会话代次已过期。
    StaleSessionEpoch,
    /// 授权引用的能力修订已经变化。
    StaleCapability,
    /// 授权引用的端口/拓扑代次已经变化。
    StaleTopologyEpoch,
    /// 授权引用的单板运行状态代次已经变化。
    StaleOperationalEpoch,
    /// 授权内容与扫描意图、Prepare 清单或会话不匹配。
    AuthorizationMismatch,
    /// 单板当前已有互斥操作，不能接受新请求。
    Busy,
    /// 单板或上层交付缓冲区没有足够的有界资源。
    ResourceExhausted,
    /// 适配器检测到调用顺序或数据交付违反契约。
    ContractViolation,
    /// 会话已经关闭。
    Closed
};

/// BoardPort 返回的类型化错误；适配边界不依赖异常传递失败。
struct BoardError final {
    BoardErrc code{BoardErrc::ContractViolation};
};

/// 发现阶段返回的一块可打开单板。
struct BoardInventoryEntry final {
    /// Provider 私有的稳定选择值，随后原样传给 BoardOpenRequest。
    std::uint32_t selector{0U};
};

/// 单次发现结果的固定容量快照。
struct BoardInventorySnapshot final {
    std::array<BoardInventoryEntry, 4U> entries{};
    /// entries 中有效元素数量，范围为 [0, entries.size()]。
    std::size_t count{0U};
};

/// 单板发现请求。
struct BoardDiscoveryRequest final {
    /// 调用者愿意接收的最大条目数；必须大于 0。
    std::size_t maximum_entries{1U};
};

/// 打开单板会话所需的选择值和版本约束。
struct BoardOpenRequest final {
    /// 来自 BoardInventoryEntry 的 Provider 私有选择值。
    std::uint32_t selector{0U};
    /// 调用者能够接受的 BoardPort 契约版本。
    BoardContractVersion accepted_contract{};
};

/// 某个已打开会话在一个确定时刻的能力与状态版本快照。
///
/// 上层必须用该快照签发 PrepareAuthorization；任何相关 epoch/revision
/// 发生变化后，旧授权都不能继续用于新请求。
struct CapabilitySnapshot final {
    BoardContractVersion contract{};
    BoardSessionId session_id{};
    /// 会话重新建立时变化，用于淘汰旧会话授权。
    std::uint64_t session_epoch{1U};
    /// 支持的频率、点数等能力变化时递增。
    std::uint64_t capability_revision{1U};
    /// 端口连接或信号路径拓扑变化时递增。
    std::uint64_t topology_epoch{1U};
    /// 影响执行安全性的单板运行状态变化时递增。
    std::uint64_t operational_epoch{1U};
    /// 对完整能力快照的稳定摘要。
    core::StrongDigest digest{};
    /// 单次逻辑扫描支持的最大点数。
    std::uint32_t maximum_points{0U};
};

/// 上层希望单板准备的最小扫描意图。
struct SweepIntent final {
    /// 扫描点数；必须大于 0 且不超过 CapabilitySnapshot::maximum_points。
    std::uint32_t point_count{0U};
    /// 起始频率，单位 Hz，必须大于 0。
    double start_hz{0.0};
    /// 终止频率，单位 Hz；多点扫描时必须大于 start_hz。
    double stop_hz{0.0};
    /// 对完整、冻结扫描意图的摘要，用于绑定授权和 Prepare 结果。
    core::StrongDigest digest{};
};

/// 允许单板为特定会话和扫描意图执行 Prepare 的一次性授权。
///
/// 对象为 move-only。移动后源对象失效；begin_prepare() 拒绝时必须通过
/// ReclaimedPrepareInputs 返还，接受时所有权转移给单板会话。
class PrepareAuthorization final {
public:
    /// 签发 Prepare 授权。
    /// @param session_id 目标单板会话 ID。
    /// @param session_epoch 签发时的会话代次。
    /// @param capability_revision 签发时的能力修订号。
    /// @param topology_epoch 签发时的拓扑代次。
    /// @param operational_epoch 签发时的运行状态代次。
    /// @param intent_digest 被授权扫描意图的摘要。
    /// @return 绑定上述版本和摘要的有效 move-only 授权。
    static PrepareAuthorization issue(
        BoardSessionId session_id,
        std::uint64_t session_epoch,
        std::uint64_t capability_revision,
        std::uint64_t topology_epoch,
        std::uint64_t operational_epoch,
        core::StrongDigest intent_digest) noexcept;

    PrepareAuthorization(PrepareAuthorization&& other) noexcept;
    PrepareAuthorization& operator=(PrepareAuthorization&& other) noexcept;
    PrepareAuthorization(const PrepareAuthorization&) = delete;
    PrepareAuthorization& operator=(const PrepareAuthorization&) = delete;

    /// @return 授权尚未被移动消费时返回 true。
    bool valid() const noexcept { return valid_; }
    /// @return 授权绑定的单板会话 ID。
    BoardSessionId session_id() const noexcept { return session_id_; }
    /// @return 授权签发时的会话代次。
    std::uint64_t session_epoch() const noexcept { return session_epoch_; }
    /// @return 授权签发时的能力修订号。
    std::uint64_t capability_revision() const noexcept { return capability_revision_; }
    /// @return 授权签发时的拓扑代次。
    std::uint64_t topology_epoch() const noexcept { return topology_epoch_; }
    /// @return 授权签发时的运行状态代次。
    std::uint64_t operational_epoch() const noexcept { return operational_epoch_; }
    /// @return 被授权扫描意图的摘要。
    core::StrongDigest intent_digest() const noexcept { return intent_digest_; }

private:
    PrepareAuthorization(
        BoardSessionId session_id,
        std::uint64_t session_epoch,
        std::uint64_t capability_revision,
        std::uint64_t topology_epoch,
        std::uint64_t operational_epoch,
        core::StrongDigest intent_digest) noexcept;

    void invalidate() noexcept;

    BoardSessionId session_id_{};
    std::uint64_t session_epoch_{0U};
    std::uint64_t capability_revision_{0U};
    std::uint64_t topology_epoch_{0U};
    std::uint64_t operational_epoch_{0U};
    core::StrongDigest intent_digest_{};
    bool valid_{false};
};

/// Prepare 成功后由适配器确认的实际执行参数和版本证据。
struct PreparedExecutionManifest final {
    ManifestId id{};
    PreparedExecutionId prepared_id{};
    BoardSessionId session_id{};
    std::uint64_t session_epoch{0U};
    std::uint64_t capability_revision{0U};
    std::uint64_t topology_epoch{0U};
    std::uint64_t operational_epoch{0U};
    /// 上层原始扫描意图摘要。
    core::StrongDigest intent_digest{};
    /// 对当前清单内容的摘要，后续启动令牌和授权必须与其一致。
    core::StrongDigest manifest_digest{};
    /// 单板实际准备的点数，允许与请求值不同但必须由上层显式接受。
    std::uint32_t actual_point_count{0U};
    /// 单板实际使用的起始频率，单位 Hz。
    double actual_start_hz{0.0};
    /// 单板实际使用的终止频率，单位 Hz。
    double actual_stop_hz{0.0};
};

/// Prepare 成功后允许且仅允许启动对应已准备执行的 move-only 令牌。
class PreparedStartToken final {
public:
    /// @param session_id 生成该准备结果的会话。
    /// @param prepared_id 已准备执行 ID。
    /// @param manifest_digest 对应清单摘要。
    PreparedStartToken(
        BoardSessionId session_id,
        PreparedExecutionId prepared_id,
        core::StrongDigest manifest_digest) noexcept;
    PreparedStartToken(PreparedStartToken&& other) noexcept;
    PreparedStartToken& operator=(PreparedStartToken&& other) noexcept;
    PreparedStartToken(const PreparedStartToken&) = delete;
    PreparedStartToken& operator=(const PreparedStartToken&) = delete;

    /// @return 令牌尚未被移动消费时返回 true。
    bool valid() const noexcept { return valid_; }
    /// @return 生成该令牌的单板会话 ID。
    BoardSessionId session_id() const noexcept { return session_id_; }
    /// @return 该令牌唯一允许启动的已准备执行 ID。
    PreparedExecutionId prepared_id() const noexcept { return prepared_id_; }
    /// @return 该令牌绑定的 Prepare 清单摘要。
    core::StrongDigest manifest_digest() const noexcept { return manifest_digest_; }

private:
    void invalidate() noexcept;

    BoardSessionId session_id_{};
    PreparedExecutionId prepared_id_{};
    core::StrongDigest manifest_digest_{};
    bool valid_{false};
};

/// 独占持有 Prepare 清单的 move-only 包装，防止清单被隐式复制到多个所有者。
class PreparedManifestLease final {
public:
    /// @param manifest 要取得所有权的已准备执行清单。
    explicit PreparedManifestLease(PreparedExecutionManifest manifest) noexcept
        : manifest_(std::move(manifest)) {}
    PreparedManifestLease(PreparedManifestLease&&) noexcept = default;
    PreparedManifestLease& operator=(PreparedManifestLease&&) noexcept = default;
    PreparedManifestLease(const PreparedManifestLease&) = delete;
    PreparedManifestLease& operator=(const PreparedManifestLease&) = delete;

    /// @return 租约持有期间有效的只读清单引用。
    const PreparedExecutionManifest& manifest() const noexcept { return manifest_; }

private:
    PreparedExecutionManifest manifest_{};
};

/// Prepare 成功产生的启动令牌和清单租约。
struct PreparedExecution final {
    PreparedStartToken start_token;
    PreparedManifestLease manifest;
};

/// Prepare 成功终态。
struct PrepareSucceeded final {
    PreparedExecution execution;
};

/// Prepare 失败前适配器已完成资源清理的证据占位类型。
struct PrepareCleanupEvidence final {};
/// Prepare 失败终态；不会再产生该调用的后续回调。
struct PrepareFailed final {
    PrepareCleanupEvidence cleanup;
    BoardError error;
};

/// Prepare 尚在排空底软资源时移交给上层的所有权占位类型。
struct BoardPrepareDrainOwner final {};
/// Prepare 进入排空流程的终态分支。
struct PrepareDraining final {
    BoardPrepareDrainOwner owner;
};

/// Prepare 接受后恰好回调一次的互斥终态。
using PrepareTerminal = std::variant<PrepareSucceeded, PrepareFailed, PrepareDraining>;

/// 接收 Prepare 异步终态的接口。
class PrepareSink {
public:
    virtual ~PrepareSink() = default;
    /// @param terminal Prepare 的唯一终态；回调取得其中 move-only 资源的所有权。
    /// @note 仅 begin_prepare() 返回 PrepareAccepted 后调用，且不会在
    ///       begin_prepare() 的调用栈内联触发。
    virtual void on_terminal(PrepareTerminal&& terminal) noexcept = 0;
};

/// move-only 的 Prepare 回调注册。
/// @note 不拥有 sink 对象；sink 必须存活到拒绝返还或终态回调返回。
class PrepareSinkRegistration final {
public:
    /// @param sink 接收 Prepare 终态的对象。
    explicit PrepareSinkRegistration(PrepareSink& sink) noexcept : sink_(&sink) {}
    PrepareSinkRegistration(PrepareSinkRegistration&& other) noexcept;
    PrepareSinkRegistration& operator=(PrepareSinkRegistration&& other) noexcept;
    PrepareSinkRegistration(const PrepareSinkRegistration&) = delete;
    PrepareSinkRegistration& operator=(const PrepareSinkRegistration&) = delete;

    /// @return 注册尚未被移动消费时返回 true。
    bool valid() const noexcept { return sink_ != nullptr; }
    /// @return 已注册接收器；调用前必须保证 valid() 为 true。
    PrepareSink& sink() const noexcept { return *sink_; }

private:
    PrepareSink* sink_{nullptr};
};

/// Prepare 请求已被单板接受排队的同步回执。
struct PrepareAccepted final {
    PrepareCallId call{};
};

/// Prepare 同步拒绝时原样返还给调用者的所有输入。
struct ReclaimedPrepareInputs final {
    SweepIntent intent{};
    PrepareAuthorization authorization;
    PrepareSinkRegistration sink;
};

/// Prepare 未被接受的同步结果；返回后不得再调用对应 sink。
struct PrepareRejected final {
    BoardError error{};
    ReclaimedPrepareInputs reclaimed;
};

/// begin_prepare() 的同步结果：接受排队，或拒绝并返还全部输入。
using PrepareSubmission = std::variant<PrepareAccepted, PrepareRejected>;

/// 证明上层当前仍有能力持续接收采集数据的短期凭据。
struct AcquisitionContinuationAttestation final {
    /// 对接收容量/上下文的摘要。
    core::StrongDigest digest{};
    /// 按 BoardPort 时基表示的失效时刻；0 表示无效。
    std::uint64_t expires_at{0U};

    /// @return 摘要和过期时间均有效时返回 true。
    bool valid() const noexcept { return digest.valid() && expires_at != 0U; }
};

/// 允许启动某个 PreparedExecution 的一次性 move-only 授权。
///
/// 授权把启动动作绑定到会话、清单、运行状态代次和上层接收能力，
/// 避免使用过期 Prepare 结果启动不可安全交付的采集。
class StartAuthorization final {
public:
    /// 签发启动授权。
    /// @param session_id 目标单板会话。
    /// @param prepared_id 要启动的已准备执行。
    /// @param manifest_digest 对应 Prepare 清单摘要。
    /// @param operational_epoch 签发时的单板运行状态代次。
    /// @param continuation 上层能够继续接收数据的有效证明。
    /// @return 绑定上述条件的有效 move-only 授权。
    static StartAuthorization issue(
        BoardSessionId session_id,
        PreparedExecutionId prepared_id,
        core::StrongDigest manifest_digest,
        std::uint64_t operational_epoch,
        AcquisitionContinuationAttestation continuation) noexcept;

    StartAuthorization(StartAuthorization&& other) noexcept;
    StartAuthorization& operator=(StartAuthorization&& other) noexcept;
    StartAuthorization(const StartAuthorization&) = delete;
    StartAuthorization& operator=(const StartAuthorization&) = delete;

    /// @return 授权尚未被移动消费时返回 true。
    bool valid() const noexcept { return valid_; }
    /// @return 授权绑定的单板会话 ID。
    BoardSessionId session_id() const noexcept { return session_id_; }
    /// @return 授权允许启动的已准备执行 ID。
    PreparedExecutionId prepared_id() const noexcept { return prepared_id_; }
    /// @return 授权绑定的 Prepare 清单摘要。
    core::StrongDigest manifest_digest() const noexcept { return manifest_digest_; }
    /// @return 授权签发时的单板运行状态代次。
    std::uint64_t operational_epoch() const noexcept { return operational_epoch_; }
    /// @return 授权内的接收能力证明只读引用；生命周期不超过当前授权对象。
    const AcquisitionContinuationAttestation& continuation() const noexcept {
        return continuation_;
    }

private:
    StartAuthorization(
        BoardSessionId session_id,
        PreparedExecutionId prepared_id,
        core::StrongDigest manifest_digest,
        std::uint64_t operational_epoch,
        AcquisitionContinuationAttestation continuation) noexcept;
    void invalidate() noexcept;

    BoardSessionId session_id_{};
    PreparedExecutionId prepared_id_{};
    core::StrongDigest manifest_digest_{};
    std::uint64_t operational_epoch_{0U};
    AcquisitionContinuationAttestation continuation_{};
    bool valid_{false};
};

/// 单次 Run 数据交付预算的 move-only 凭证。
///
/// begin_run() 接受后由适配器持有，并在发送终态前 retire；同步拒绝时
/// 必须原样返还给调用者，防止交付预算泄漏或被重复使用。
class RunDeliveryGrant final {
public:
    /// @param grant_id 非 0 的交付许可 ID；0 会创建无效凭证。
    explicit RunDeliveryGrant(std::uint64_t grant_id) noexcept
        : grant_id_(grant_id), valid_(grant_id != 0U) {}
    RunDeliveryGrant(RunDeliveryGrant&& other) noexcept;
    RunDeliveryGrant& operator=(RunDeliveryGrant&& other) noexcept;
    RunDeliveryGrant(const RunDeliveryGrant&) = delete;
    RunDeliveryGrant& operator=(const RunDeliveryGrant&) = delete;

    /// @return 凭证尚未被移动或注销时返回 true。
    bool valid() const noexcept { return valid_; }
    /// @return 交付许可的原始 ID。
    std::uint64_t grant_id() const noexcept { return grant_id_; }
    /// 显式注销许可，使其不能再次用于 Run。
    void retire() noexcept { invalidate(); }

private:
    void invalidate() noexcept;

    std::uint64_t grant_id_{0U};
    bool valid_{false};
};

/// 一个接收机复数采样点，采用实部/虚部笛卡尔表示。
struct ComplexSample final {
    float real{0.0F};
    float imag{0.0F};

    friend bool operator==(ComplexSample lhs, ComplexSample rhs) noexcept {
        return lhs.real == rhs.real && lhs.imag == rhs.imag;
    }
};

/// 单个 BoardPort 数据块在当前契约中可携带的最大复数点数。
constexpr std::size_t kMaximumContractChunkSamples = 64U;

/// 独占携带一块接收机采样数据的 move-only 租约。
///
/// 当前实现把样本复制进固定数组，接口仍采用租约语义，以便真实适配器后续
/// 替换为 DMA/缓冲池所有权而不改变上层数据流契约。
class AcquisitionChunkLease final {
public:
    /// 创建数据块租约。
    /// @param samples 固定容量样本源。
    /// @param size 有效样本数，范围必须为 [1, kMaximumContractChunkSamples]；
    ///        越界时创建无效租约且 size() 为 0。
    AcquisitionChunkLease(
        const std::array<ComplexSample, kMaximumContractChunkSamples>& samples,
        std::size_t size) noexcept;
    AcquisitionChunkLease(AcquisitionChunkLease&& other) noexcept;
    AcquisitionChunkLease& operator=(AcquisitionChunkLease&& other) noexcept;
    AcquisitionChunkLease(const AcquisitionChunkLease&) = delete;
    AcquisitionChunkLease& operator=(const AcquisitionChunkLease&) = delete;

    /// @return 租约包含合法的非空数据块时返回 true。
    bool valid() const noexcept { return valid_; }
    /// @return 当前租约内的有效样本数。
    std::size_t size() const noexcept { return size_; }
    /// 按数据块内偏移读取样本。
    /// @param index 范围必须为 [0, size())；函数不执行边界检查。
    /// @return 对应复数样本的只读引用，生命周期不超过当前租约。
    const ComplexSample& operator[](std::size_t index) const noexcept {
        return samples_[index];
    }

private:
    void invalidate() noexcept;

    std::array<ComplexSample, kMaximumContractChunkSamples> samples_{};
    std::size_t size_{0U};
    bool valid_{false};
};

/// 原始接收机波形种类；尚未经过比值、校准或格式化处理。
enum class ReceiverWave {
    /// 入射参考接收机 A 波。
    IncidentA,
    /// 响应测量接收机 B 波。
    ResponseB
};

/// 可按位组合的接收机数据质量异常。
enum class ReceiverQualityFlag : std::uint32_t {
    /// 接收机幅度过载，相关测量点可能失真。
    Overload = 1U << 0U,
    /// 接收机未锁定。
    ReceiverUnlocked = 1U << 1U,
    /// 激励源未达到目标电平。
    SourceUnleveled = 1U << 2U,
    /// 参考时基未锁定。
    TimebaseUnlocked = 1U << 3U
};

/// 与一个数据块整体关联的质量标记集合。
struct ChunkQuality final {
    std::uint32_t flags{0U};

    /// @param flag 要检查的单个质量标记。
    /// @return flags 中包含该标记时返回 true。
    bool has(ReceiverQualityFlag flag) const noexcept {
        return (flags & static_cast<std::uint32_t>(flag)) != 0U;
    }
};

/// BoardPort 向上层交付的一块有序原始接收机观测数据。
struct ReceiverObservationChunk final {
    ManifestId manifest_id{};
    PreparedExecutionId prepared_id{};
    BoardRunId run_id{};
    RunGeneration run_generation{};
    ChunkSequence sequence{};
    ReceiverWave wave{ReceiverWave::IncidentA};
    /// 本块第一个样本在完整逻辑扫描中的零基点索引。
    std::uint32_t point_begin{0U};
    /// move-only 样本数据；on_chunk() 接收方取得其所有权。
    AcquisitionChunkLease payload;
    ChunkQuality quality{};
};

/// 上层接收数据块后立即反馈给单板的流控决定。
enum class ChunkIngressDisposition {
    /// 数据块已被上层可靠接收，可继续交付。
    Accepted,
    /// 上层容量承诺被突破，要求终止当前 Run。
    AbortRunCapacityBreach,
    /// 数据块顺序、身份或内容违反协议，要求终止当前 Run。
    AbortRunProtocolViolation
};

/// 单板 Run 的非终态阶段。
enum class BoardRunPhase {
    /// 单板正在启动已准备执行。
    Starting,
    /// 单板正在采集并可能交付数据块。
    Acquiring
};

/// 一次 Run 的阶段变化事件。
struct BoardRunPhaseEvent final {
    BoardRunId run_id{};
    RunGeneration generation{};
    BoardRunPhase phase{BoardRunPhase::Starting};
};

/// 单板 Run 的最终结果分类。
enum class RunTerminalKind {
    /// 所有预期数据均已完成交付。
    Completed,
    /// 单板或上层数据入口报告失败。
    Failed,
    /// Run 被外部取消或主动中止。
    Aborted
};

/// Run 的唯一终态事件。
struct BoardRunTerminal final {
    BoardRunId run_id{};
    RunGeneration generation{};
    RunTerminalKind kind{RunTerminalKind::Failed};
    /// 在终态前实际调用 on_chunk() 的次数，包括被接收方拒绝的数据块。
    std::uint32_t delivered_chunks{0U};
};

/// 接收 Run 阶段、原始数据和终态的异步接口。
class BoardRunSink {
public:
    virtual ~BoardRunSink() = default;

    /// 通知 Run 阶段变化。
    /// @param event 包含 Run 身份、代次和新阶段；引用仅在回调期间有效。
    virtual void on_phase(const BoardRunPhaseEvent& event) noexcept = 0;

    /// 交付一个有序原始接收机数据块。
    /// @param chunk 数据块及其 payload 的所有权转移给接收方。
    /// @return 接收决定；非 Accepted 会要求适配器停止继续交付并发送失败终态。
    virtual ChunkIngressDisposition on_chunk(
        ReceiverObservationChunk&& chunk) noexcept = 0;

    /// 通知 Run 唯一终态。
    /// @param terminal 终态对象的所有权转移给接收方。
    /// @note 终态之后不得再调用 on_phase()、on_chunk() 或 on_terminal()。
    virtual void on_terminal(BoardRunTerminal&& terminal) noexcept = 0;
};

/// move-only 的 Run 回调注册。
/// @note 不拥有 sink 对象；sink 必须存活到同步拒绝返还或 Run 终态回调返回。
class BoardRunSinkRegistration final {
public:
    /// @param sink 接收 Run 事件和数据的对象。
    explicit BoardRunSinkRegistration(BoardRunSink& sink) noexcept : sink_(&sink) {}
    BoardRunSinkRegistration(BoardRunSinkRegistration&& other) noexcept;
    BoardRunSinkRegistration& operator=(BoardRunSinkRegistration&& other) noexcept;
    BoardRunSinkRegistration(const BoardRunSinkRegistration&) = delete;
    BoardRunSinkRegistration& operator=(const BoardRunSinkRegistration&) = delete;

    /// @return 注册尚未被移动消费时返回 true。
    bool valid() const noexcept { return sink_ != nullptr; }
    /// @return 已注册接收器；调用前必须保证 valid() 为 true。
    BoardRunSink& sink() const noexcept { return *sink_; }

private:
    BoardRunSink* sink_{nullptr};
};

/// Run 已被单板接受的同步回执。
struct RunAccepted final {
    BoardRunId run{};
    RunGeneration generation{};
};

/// Run 同步拒绝时原样返还给调用者的全部 move-only 输入。
struct ReclaimedRunInputs final {
    PreparedStartToken prepared;
    StartAuthorization authorization;
    RunDeliveryGrant delivery;
    BoardRunSinkRegistration sink;
};

/// Run 未被接受的同步结果；返回后不得产生对应 Run 的任何回调。
struct RunRejected final {
    BoardError error{};
    ReclaimedRunInputs reclaimed;
};

/// begin_run() 的同步结果：接受排队，或拒绝并返还全部输入。
using RunSubmission = std::variant<RunAccepted, RunRejected>;

/// 单板执行面接口，负责 Prepare/Run 两阶段异步扫描协议。
///
/// 同步返回 Accepted 只表示适配器取得了输入所有权，最终结果通过 sink 回调；
/// 同步返回 Rejected 时适配器没有取得所有权，必须在结果中返还全部 move-only 输入。
class BoardExecutionPort {
public:
    virtual ~BoardExecutionPort() = default;

    /// @return 调用时刻的最新能力和状态版本快照。
    virtual CapabilitySnapshot capabilities() const noexcept = 0;

    /// 请求单板验证并准备一项冻结扫描意图。
    /// @param call 非 0 且由上层分配的 Prepare 调用 ID。
    /// @param intent 要准备的扫描意图，按值传入以便拒绝时完整返还。
    /// @param authorization 与当前能力版本及 intent.digest 匹配的一次性授权。
    /// @param sink Prepare 唯一终态的回调注册。
    /// @return PrepareAccepted 表示输入所有权已经转移并将在未来回调一次；
    ///         PrepareRejected 表示未接受，并连同错误返还 intent、authorization、sink。
    virtual PrepareSubmission begin_prepare(
        PrepareCallId call,
        SweepIntent intent,
        PrepareAuthorization&& authorization,
        PrepareSinkRegistration&& sink) noexcept = 0;

    /// 启动一个已经 Prepare 成功的采集执行。
    /// @param run 非 0 且由上层分配的 Run ID。
    /// @param generation 非 0 的 Run 代次，用于丢弃迟到事件。
    /// @param prepared Prepare 成功返回且与 authorization 匹配的启动令牌。
    /// @param authorization 绑定会话、清单、运行状态和接收能力的启动授权。
    /// @param delivery 上层预留的数据交付预算凭证。
    /// @param sink 接收阶段、A/B 原始数据块和唯一终态的注册。
    /// @return RunAccepted 表示所有 move-only 输入已被消费且后续异步回调；
    ///         RunRejected 表示未启动，并原样返还全部输入且永不回调。
    virtual RunSubmission begin_run(
        BoardRunId run,
        RunGeneration generation,
        PreparedStartToken&& prepared,
        StartAuthorization&& authorization,
        RunDeliveryGrant&& delivery,
        BoardRunSinkRegistration&& sink) noexcept = 0;
};

/// 一次已打开单板会话的所有功能面入口。
/// @note 当前纵切只暴露 execution 功能面，后续安全和维护面将独立扩展。
class BoardSession {
public:
    virtual ~BoardSession() = default;

    /// @return 与当前会话同寿命的执行面引用。
    virtual BoardExecutionPort& execution() noexcept = 0;

    /// @return open() 成功时捕获的初始能力快照引用。
    virtual const CapabilitySnapshot& initial_capabilities() const noexcept = 0;
};

/// 独占拥有 BoardSession 的 RAII 会话句柄。
class OpenedBoard final {
public:
    /// @param owner 要接管的非空会话所有者。
    explicit OpenedBoard(std::unique_ptr<BoardSession> owner) noexcept
        : owner_(std::move(owner)) {}
    OpenedBoard(OpenedBoard&&) noexcept = default;
    OpenedBoard& operator=(OpenedBoard&&) noexcept = default;
    OpenedBoard(const OpenedBoard&) = delete;
    OpenedBoard& operator=(const OpenedBoard&) = delete;

    /// @return 会话执行面引用；生命周期不超过当前 OpenedBoard。
    BoardExecutionPort& execution() noexcept { return owner_->execution(); }
    /// @return 会话打开时的初始能力快照引用。
    const CapabilitySnapshot& initial_capabilities() const noexcept {
        return owner_->initial_capabilities();
    }

private:
    std::unique_ptr<BoardSession> owner_;
};

/// 单板适配器工厂边界；真实单板和 Mock 都通过该接口接入上层。
class BoardProvider {
public:
    virtual ~BoardProvider() = default;

    /// 枚举当前 Provider 可打开的单板。
    /// @param request 包含调用者愿意接收的条目上限。
    /// @return 成功时返回固定容量清单；请求非法或底层发现失败时返回 BoardError。
    virtual core::Result<BoardInventorySnapshot, BoardError> discover(
        const BoardDiscoveryRequest& request) noexcept = 0;

    /// 打开一个独占单板会话。
    /// @param request 包含 discover() 返回的 selector 和可接受契约版本。
    /// @return 成功时返回独占 OpenedBoard；选择值、版本或资源不满足时返回 BoardError。
    virtual core::Result<OpenedBoard, BoardError> open(
        const BoardOpenRequest& request) noexcept = 0;
};

}  // namespace vna::board
