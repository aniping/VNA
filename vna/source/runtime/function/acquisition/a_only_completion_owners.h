#pragma once

#include "runtime/function/acquisition/acquisition_admission.h"
#include "runtime/function/acquisition/candidate_commit_lease.h"

#include <optional>

namespace vna::acquisition {

/// worker 成功后由 L2 保持的 A-only completion 与 disabled Preview owner 聚合。
///
/// Store commit 只接收 CandidateCommitLease，本对象不能进入 Store。L2 必须先
/// 取得匹配 snapshot ID 的 commit receipt，再调用 finalize_published()；提交失败
/// 则先让失败事实可见，再调用 finalize_failed()。
class AOnlyCompletionOwners final {
public:
    /// 接管首次派发前取得的全部上层 owner。
    /// @param resources 已完成 Manifest 收窄、仍持有 completion/Preview 的租约。
    /// @param expected_snapshot 仅允许成功终结的预签发 A snapshot ID。
    AOnlyCompletionOwners(
        AcquisitionAdmissionPool::Lease&& resources,
        CompletedSweepId expected_snapshot) noexcept;
    /// 转移全部 owner。
    /// @param other 所有权来源；构造完成后失效，只能析构。
    AOnlyCompletionOwners(AOnlyCompletionOwners&& other) noexcept;
    /// 禁止移动赋值，避免覆盖仍等待 Store receipt 或失败事实的 owner。
    AOnlyCompletionOwners& operator=(AOnlyCompletionOwners&& other) = delete;
    /// purpose-specific owner 不能复制。
    AOnlyCompletionOwners(const AOnlyCompletionOwners&) = delete;
    /// purpose-specific owner 不能复制赋值。
    AOnlyCompletionOwners& operator=(const AOnlyCompletionOwners&) = delete;
    /// 归还仍未终结的固定容量；析构不等价于 Store receipt 或成功发布。
    ~AOnlyCompletionOwners() = default;

    /// @return owner 聚合仍有效且尚未终结时返回 true。
    bool valid() const noexcept;

    /// 在 Store 成功发布匹配 A 后终结 completion 与 disabled Preview owner。
    /// @param published_snapshot Store receipt 中的 A snapshot ID。
    /// @return ID 匹配且首次成对终结时返回 true；否则不消费 owner 并返回 false。
    bool finalize_published(CompletedSweepId published_snapshot) noexcept;

    /// 在候选已 abort 且失败事实已原子可见后成对失败终结 owner。
    /// @return 首次终结返回 true；无 owner 或重复调用返回 false。
    bool finalize_failed() noexcept;

private:
    std::optional<AcquisitionAdmissionPool::Lease> resources_{};
    CompletedSweepId expected_snapshot_{};
};

}  // namespace vna::acquisition
