#pragma once

#include "runtime/core/base/result.h"
#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vna::acquisition {

/// 固定容量的 A-only 上层采集资源准入池。
///
/// 每个槽位把 A 输出/candidate metadata、Buffer、Ingress、A-only completion、
/// disabled Preview 和一次性精确收窄能力建模为分别具名的 move-only RAII
/// owner。Board Prepare/Run call、队列和 callback sink 容量由同一提交另外取得的
/// board::BoardExecutionReservation 真实预占，二者都必须早于首次 dispatch。
class AcquisitionAdmissionPool final {
    enum class LocalResource : std::uint16_t {
        AOutput = 1U << 0U,
        CandidateMetadata = 1U << 1U,
        AcquisitionBuffer = 1U << 2U,
        AcquisitionIngress = 1U << 3U,
        AOnlyCompletionOwner = 1U << 4U,
        DisabledPreviewOwner = 1U << 5U,
        ExactFinalizationCapability = 1U << 6U
    };

    class LocalResourceOwner {
    public:
        LocalResourceOwner(LocalResourceOwner&& other) noexcept;
        LocalResourceOwner& operator=(LocalResourceOwner&& other) noexcept;
        LocalResourceOwner(const LocalResourceOwner&) = delete;
        LocalResourceOwner& operator=(const LocalResourceOwner&) = delete;
        ~LocalResourceOwner();

        bool valid() const noexcept;
        bool retire() noexcept;

    protected:
        LocalResourceOwner(
            AcquisitionAdmissionPool& owner,
            std::size_t slot,
            std::uint64_t generation,
            LocalResource resource) noexcept;

    private:
        void release() noexcept;
        void invalidate() noexcept;

        AcquisitionAdmissionPool* owner_{nullptr};
        std::size_t slot_{0U};
        std::uint64_t generation_{0U};
        LocalResource resource_{LocalResource::AOutput};
    };

    /// 用编译期 tag 生成彼此不可混用的私有 owner 类型，同时复用相同 RAII 机制。
    template <LocalResource Resource>
    class TaggedLocalResourceOwner final : public LocalResourceOwner {
    public:
        TaggedLocalResourceOwner(
            AcquisitionAdmissionPool& owner,
            std::size_t slot,
            std::uint64_t generation) noexcept
            : LocalResourceOwner(owner, slot, generation, Resource) {}
    };

    using AOutputOwner =
        TaggedLocalResourceOwner<LocalResource::AOutput>;
    using CandidateMetadataOwner =
        TaggedLocalResourceOwner<LocalResource::CandidateMetadata>;
    using AcquisitionBufferOwner =
        TaggedLocalResourceOwner<LocalResource::AcquisitionBuffer>;
    using AcquisitionIngressOwner =
        TaggedLocalResourceOwner<LocalResource::AcquisitionIngress>;
    using AOnlyCompletionOwner =
        TaggedLocalResourceOwner<LocalResource::AOnlyCompletionOwner>;
    using DisabledPreviewOwner =
        TaggedLocalResourceOwner<LocalResource::DisabledPreviewOwner>;
    using ExactFinalizationOwner =
        TaggedLocalResourceOwner<LocalResource::ExactFinalizationCapability>;

public:
    /// 编译期最多同时保留的上层采集资源集合数。
    static constexpr std::size_t kMaximumLeases = 16U;

    /// 一次保守资源准入所冻结的紧凑元数据。
    struct Claim final {
        /// 与 FrozenSweepJob 和 SweepIntent 共同绑定的非 0 摘要。
        core::StrongDigest plan_digest{};
        /// 签发本次资源 envelope 时读取的单板 capability cut。
        board::CapabilitySnapshot capabilities{};
        /// 本租约允许实际 Manifest 使用的最大点数。
        std::uint32_t maximum_points{0U};
        /// 预留 Ingress 能可靠接收的最大正式 chunk 数。
        std::uint32_t maximum_chunks{0U};
        /// 保守 envelope 允许的最低实际起始频率，单位 Hz。
        double minimum_start_hz{0.0};
        /// 保守 envelope 允许的最高实际终止频率，单位 Hz。
        double maximum_stop_hz{0.0};
    };

    /// 上层采集资源准入失败原因。
    enum class Errc {
        /// 摘要、能力版本或容量上界非法。
        InvalidClaim,
        /// 固定上层资源槽已经全部占用。
        ResourceExhausted
    };

    /// 上层采集资源准入错误。
    struct Error final {
        /// 调用者可稳定映射的准入失败分类。
        Errc code{Errc::InvalidClaim};
    };

    /// 当前资源使用与所有权终结计数的只读快照。
    struct Snapshot final {
        /// 当前至少仍有一个具名 owner 占用、不可复用的槽位数。
        std::size_t in_use{0U};
        /// A-only completion 与 disabled Preview owner 成对失败终结的累计次数。
        std::uint64_t failure_finalizations{0U};
        /// A-only completion 与 disabled Preview owner 在 A commit receipt 后
        /// 成对成功终结的累计次数。
        std::uint64_t success_finalizations{0U};
    };

    /// 一项 generation-bound 的 move-only 上层采集资源租约。
    /// @note 签发 Lease 的 AcquisitionAdmissionPool 必须比 Lease 及其移动目标
    ///       活得更久；析构分别归还仍持有的具名本地容量。
    class Lease final {
    public:
        /// 转移全部具名 owner。
        /// @param other 租约来源；构造后 other 失效。
        Lease(Lease&& other) noexcept = default;
        /// 先归还当前具名 owner，再转移来源租约。
        /// @param other 租约来源；赋值后 other 失效。
        /// @return 当前租约引用。
        Lease& operator=(Lease&& other) noexcept = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        /// 各具名 RAII owner 分别归还仍持有的固定容量。
        ~Lease() = default;

        /// @return A/candidate/Buffer/Ingress 及尚未终结的 completion/Preview
        ///         owner 均仍有效时返回 true；精确收窄能力可已被正常消费。
        bool valid() const noexcept;

        /// @return 首次 dispatch 要求的七项具名上层 owner 当前全部存在时返回 true。
        bool owns_pre_dispatch_resources() const noexcept;

        /// 使用 Prepare 返回的实际 Manifest 消费精确收窄 capability。
        /// @param manifest Board 返回的不可变实际执行事实；不转移所有权。
        /// @return Manifest 的身份、会话、版本、摘要、频率和点数均在 Claim 内
        ///         时返回 true；否则返回 false。
        /// @note one-shot capability 在首次调用时即消费，无论校验成功或失败都
        ///       不能重试；失败时其余 owner 仍由调用者保留到失败终结。
        bool narrow_to(const board::PreparedExecutionManifest& manifest) noexcept;

        /// 在失败事实已经由 Store 原子提交后终结 A-only 与 disabled Preview owner。
        /// @return 首次成功终结时返回 true；无效租约或重复调用返回 false。
        /// @note 本调用不立即复用池槽；其余 owner 析构后才完全归还槽位。
        bool finalize_failure() noexcept;

        /// 在 Store 已返回匹配 A publication receipt 后终结 completion/Preview owner。
        /// @return 首次成功终结时返回 true；无效租约或重复调用返回 false。
        /// @note 与 finalize_failure() 互斥；其余 A/candidate/Buffer/Ingress owner
        ///       仍由本 Lease 析构归还固定容量。
        bool finalize_success() noexcept;

    private:
        friend class AcquisitionAdmissionPool;
        Lease(
            AcquisitionAdmissionPool& owner,
            std::size_t slot,
            std::uint64_t generation,
            Claim claim) noexcept;

        AcquisitionAdmissionPool* owner_{nullptr};
        Claim claim_{};
        AOutputOwner a_output_;
        CandidateMetadataOwner candidate_metadata_;
        AcquisitionBufferOwner buffer_;
        AcquisitionIngressOwner ingress_;
        AOnlyCompletionOwner a_only_completion_;
        DisabledPreviewOwner disabled_preview_;
        ExactFinalizationOwner exact_finalization_;
    };

    /// @param capacity 可同时保留的采集数量；超过 kMaximumLeases 时截断。
    explicit AcquisitionAdmissionPool(std::size_t capacity) noexcept;

    /// 在首次 dispatch 前原子取得一项采集所需的全部具名上层资源。
    /// @param claim 与冻结计划和 Board 能力版本绑定的保守上界。
    /// @return 成功时返回自动归还的 move-only Lease；Claim 非法或容量不足时
    ///         返回类型化错误，池内状态不发生部分变化。成功对象不得比当前池
    ///         活得更久。
    core::Result<Lease, Error> reserve(Claim claim) noexcept;

    /// @return 当前占用数以及成功、失败终结 owner 的累计计数副本。
    Snapshot inspect() const noexcept;

private:
    using ResourceMask = std::uint16_t;
    static constexpr ResourceMask kAllLocalResources = (1U << 7U) - 1U;

    struct Slot final {
        ResourceMask resources{0U};
        std::uint64_t generation{0U};
    };

    static constexpr ResourceMask mask(LocalResource resource) noexcept {
        return static_cast<ResourceMask>(resource);
    }
    void release_resource(
        std::size_t slot,
        std::uint64_t generation,
        LocalResource resource) noexcept;
    void record_failure_finalization() noexcept;
    void record_success_finalization() noexcept;

    std::array<Slot, kMaximumLeases> slots_{};
    std::size_t capacity_{0U};
    std::uint64_t next_generation_{1U};
    std::uint64_t failure_finalizations_{0U};
    std::uint64_t success_finalizations_{0U};
};

}  // namespace vna::acquisition
