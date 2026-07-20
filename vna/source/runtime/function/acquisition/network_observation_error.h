#pragma once

#include "runtime/function/acquisition/candidate_commit_lease.h"
#include "runtime/platform/board/board_port.h"

#include <cstdint>

namespace vna::acquisition {

/// 单板观测账本失败的稳定分类。
enum class NetworkObservationErrc {
    /// Manifest 的实际轴或必需观测图不完整、重复或超出当前有界切片。
    InvalidManifest,
    /// 数据块身份、序号、payload 或当前 Builder 状态与 Manifest/Run 不匹配。
    InvalidChunk,
    /// Ingress 已接管但因 payload/容量契约拒绝了正式数据块。
    IngressRejected,
    /// 同一观测的同一完整点范围被再次交付，内容或质量不能覆盖首次事实。
    ConflictingDuplicate,
    /// 新数据块与已接受范围部分相交，无法形成唯一逐点来源。
    Overlap,
    /// 数据块范围超出 Manifest 为对应观测声明的点范围。
    OutOfRange,
    /// Run terminal 身份、数量或种类违反密封契约。
    InvalidTerminal,
    /// 成功 terminal 到达时仍缺少必需观测或完整点覆盖。
    IncompleteCoverage,
    /// Builder 已经密封，不能再次接收或密封。
    AlreadySealed
};

/// 某项必需观测在失败时的有界覆盖摘要。
struct ObservationCoverageSummary final {
    /// Manifest 中必需观测总数。
    std::uint32_t expected_observations{0U};
    /// 失败前已经完整闭合的必需观测数。
    std::uint32_t complete_observations{0U};
    /// 相关观测由 Manifest 声明的总点数。
    std::uint32_t expected_points{0U};
    /// 相关观测已经由互不重叠的有效 chunk 唯一覆盖的点数。
    std::uint32_t accepted_unique_points{0U};
    /// 相关观测已经接受并持有的正式 chunk 数。
    std::uint32_t accepted_chunks{0U};
    /// 第一段未覆盖范围的零基起点；没有缺口时等于 expected_points。
    std::uint32_t first_missing_point{0U};
    /// 从 first_missing_point 开始连续缺失的点数；完整覆盖时为 0。
    std::uint32_t missing_point_count{0U};
};

/// NetworkObservationBuilder 返回并可随失败 Event 保存的类型化账本证据。
struct NetworkObservationError final {
    /// 调用者无需解析文本即可处理的稳定分类。
    NetworkObservationErrc code{NetworkObservationErrc::InvalidManifest};
    /// 账本绑定的 Prepared Manifest 身份。
    board::ManifestId manifest{};
    /// 与 Manifest 同源的 Prepared execution 身份。
    board::PreparedExecutionId prepared{};
    /// 账本绑定的 Board Run 身份。
    board::BoardRunId run{};
    /// 与 run 配对、用于隔离迟到数据的 generation。
    board::RunGeneration generation{};
    /// observation 与 coverage 是否描述一项 Manifest 必需观测。
    bool has_observation{false};
    /// has_observation 为 true 时的类型化必需观测身份与期望点数。
    board::PreparedObservationSpec observation{};
    /// offending_chunk 是否保存本次被拒绝的块头证据。
    bool has_offending_chunk{false};
    /// has_offending_chunk 为 true 时的来源、序号和声明范围；不持有 payload。
    BoardChunkEvidence offending_chunk{};
    /// ingress_disposition 是否保存 Ingress 对正式块的容量/协议拒绝分类。
    bool has_ingress_disposition{false};
    /// has_ingress_disposition 为 true 时的原始接收决定；不得为 Accepted。
    board::ChunkIngressDisposition ingress_disposition{
        board::ChunkIngressDisposition::Accepted};
    /// 失败时相关观测的期望、已接受与首段缺口摘要。
    ObservationCoverageSummary coverage{};
    /// 是否已经观察到与本账本同 run/generation 的唯一 terminal。
    bool terminal_observed{false};
    /// terminal_observed 为 true 时保存其种类。
    board::RunTerminalKind terminal_kind{board::RunTerminalKind::Failed};
    /// terminal_observed 为 true 时保存单板声明的实际 callback 次数。
    std::uint32_t terminal_delivered_chunks{0U};
};

}  // namespace vna::acquisition
