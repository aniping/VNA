#pragma once

#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/acquisition/acquisition_ingress.h"
#include "runtime/function/acquisition/network_observation_builder.h"
#include "runtime/function/operation/operation_runtime.h"
#include "runtime/platform/board/board_port.h"

#include <optional>

namespace vna::acquisition {

/// 具名采集 Drain 在某一时刻持有的可审计所有权事实。
///
/// 本快照不携带可执行 token。runtime_completion_registered 表示同一 Runtime
/// Draining 槽仍持有可靠 completion registration；其余字段来自 L4 的真实
/// move-only owner。它只证明软件所有权守恒，不证明 abort、RF-off 或物理安全。
struct AcquisitionDrainOwnershipSnapshot final {
    /// Board Adapter 仍持有已接受 Run 的 token/grant/sink，L4 保持对应 reservation。
    bool board_run_callback_obligation{false};
    /// Prepared Manifest lease 仍由善后 owner 保活。
    bool manifest_owned{false};
    /// 未密封 Builder 及其中的正式 chunk owner 仍由善后 owner 保活。
    bool builder_owned{false};
    /// 固定 Buffer/Ingress 准入 owner 与 Ingress 队列仍不可复用。
    bool buffer_ingress_owned{false};
    /// Runtime Draining 槽仍保留原 completion registration。
    bool runtime_completion_registered{false};
    /// A-only completion owner 尚未终结。
    bool a_only_completion_owned{false};
    /// disabled Preview owner 尚未终结。
    bool disabled_preview_owned{false};
    /// one-shot ExactFinalizationCapability 已在 Manifest 精确收窄时消费。
    bool exact_finalization_consumed{false};
    /// 当前本轮资源已经由实际 Manifest 精确收窄且仍绑定原 Run。
    bool run_resources_narrowed{false};
};

/// 卡住采集进入 Draining 后的完整 L4 move-only owner。
///
/// Board Adapter 继续拥有已接受调用内部的 token、delivery grant 与 sink
/// registration；本对象拥有对应 execution reservation、Manifest、Builder、
/// Ingress 和上层资源租约。二者由相同 Run/Drain 身份关联，直到 Board terminal
/// 到达。Runtime completion registration 由 OperationRuntime 的 Draining 槽持有，
/// 并在对外快照中与本对象合并为一项完整系统所有权证明。
class AcquisitionDrainOwner final {
public:
    /// 接管进入 Draining 时仍未终结的全部 L4 资源。
    /// @param drain 非 0 DrainId；按值保存。
    /// @param work 与原 Accepted Operation 关联的非 0 WorkId；按值保存。
    /// @param run 已接受且尚未终结的 BoardRunId；按值保存。
    /// @param generation 与 run 绑定的代次；按值保存。
    /// @param ingress Board callback 与 Builder 之间的固定队列；转移所有权。
    /// @param prepared_manifest Prepared Manifest lease；转移所有权，Run Draining
    ///        路径必须存在，较早 Prepare 善后路径可以为空。
    /// @param builder 正式观测 Builder；转移所有权，Run Draining 路径必须存在。
    /// @param resources A/candidate/Buffer/Ingress/completion/Preview owner 聚合；
    ///        转移所有权，ExactFinalizationCapability 可以已经被消费。
    /// @param board_reservation 对应 Board call/queue/callback route 的执行预留；
    ///        转移所有权并保持到唯一 Drain terminal 处理结束。
    AcquisitionDrainOwner(
        runtime::DrainId drain,
        runtime::WorkId work,
        board::BoardRunId run,
        board::RunGeneration generation,
        AcquisitionIngress&& ingress,
        std::optional<board::PreparedManifestLease>&& prepared_manifest,
        std::optional<NetworkObservationBuilder>&& builder,
        AcquisitionAdmissionPool::Lease&& resources,
        board::BoardExecutionReservation&& board_reservation) noexcept;

    /// 转移全部尚未终结 owner；来源随后只可析构。
    AcquisitionDrainOwner(AcquisitionDrainOwner&& other) noexcept = default;
    AcquisitionDrainOwner& operator=(AcquisitionDrainOwner&&) = delete;
    AcquisitionDrainOwner(const AcquisitionDrainOwner&) = delete;
    AcquisitionDrainOwner& operator=(const AcquisitionDrainOwner&) = delete;

    /// @return Drain/Work、Ingress、上层资源和 Board reservation 均有效时为 true。
    bool valid() const noexcept;

    /// @return 仍由本 owner 保活的 Ingress；引用不超过本对象生命周期。
    AcquisitionIngress& ingress() noexcept { return ingress_; }

    /// @return Run Draining 路径的 Builder；较早 Prepare 路径返回 nullptr。
    NetworkObservationBuilder* builder() noexcept;

    /// @param runtime_completion_registered 同一 Runtime Draining 槽仍持有可靠
    ///        completion registration 时传 true；本函数不取得该能力所有权。
    /// @return 当前软件所有权事实的值快照。
    AcquisitionDrainOwnershipSnapshot inspect(
        bool runtime_completion_registered) const noexcept;

    /// 在 Store 已可靠记录唯一 Drained terminal 后终结 completion/Preview owner。
    /// @return 首次成功终结返回 true；重复调用或无效 owner 返回 false。
    /// @note 其余 Buffer/Ingress/Board owner 在本对象析构时归还，因此调用方必须
    ///       先提交 Drain terminal，再销毁本对象；本函数不执行 Board abort。
    bool finalize_failure() noexcept;

private:
    runtime::DrainId drain_{};
    runtime::WorkId work_{};
    board::BoardRunId run_{};
    board::RunGeneration generation_{};
    AcquisitionIngress ingress_;
    std::optional<board::PreparedManifestLease> prepared_manifest_{};
    std::optional<NetworkObservationBuilder> builder_{};
    AcquisitionAdmissionPool::Lease resources_;
    board::BoardExecutionReservation board_reservation_;
};

}  // namespace vna::acquisition
