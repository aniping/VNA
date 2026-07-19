#pragma once

#include "runtime/core/base/result.h"
#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vna::board {

/// 固定采集 BufferPool 的资源准入失败分类。
enum class AcquisitionBufferPoolErrc {
    /// grant ID、请求块数或 Pool 构造容量非法。
    InvalidReservation,
    /// 当前没有足够空闲槽完成整组原子预留。
    ResourceExhausted
};

/// AcquisitionBufferPool 返回的类型化错误。
struct AcquisitionBufferPoolError final {
    /// 调用者可稳定映射的准入失败分类。
    AcquisitionBufferPoolErrc code{AcquisitionBufferPoolErrc::InvalidReservation};
};

/// 为不可转移的底软源内存提供一次复制回退的固定容量 BufferPool。
///
/// Pool 必须比其签发的 RunDeliveryGrant 和 AcquisitionChunkLease 活得更久。
/// reserve_delivery() 在首次派发前原子冻结整组槽；Adapter 只能通过 grant 的
/// copy_fallback() 在回调前复制一次，后续层只移动指向槽位的 lease。
class AcquisitionBufferPool final {
public:
    /// 单个 Pool 可静态持有的最大数据块数；A-only 当前实际配置使用 2。
    static constexpr std::size_t kMaximumBuffers =
        kMaximumPreparedObservations;

    /// Pool 当前资源占用与回退复制次数的只读快照。
    struct Snapshot final {
        /// 构造时启用的固定槽位数。
        std::size_t capacity{0U};
        /// 已预留给 grant、尚未转换为 lease 的槽位数。
        std::size_t reserved{0U};
        /// 已由 move-only AcquisitionChunkLease 独占的槽位数。
        std::size_t leased{0U};
        /// 从不可转移源内存复制到 Pool 的累计数据块次数。
        std::uint64_t copy_operations{0U};
    };

    /// 建立固定容量 Pool。
    /// @param capacity 启用槽位数，范围必须为 [1, kMaximumBuffers]；越界时
    ///        Pool 无有效容量，后续预留返回 InvalidReservation。
    explicit AcquisitionBufferPool(std::size_t capacity) noexcept;
    /// Pool 地址被已签发 grant/lease 引用，禁止移动。
    AcquisitionBufferPool(AcquisitionBufferPool&& other) = delete;
    /// Pool 地址被已签发 grant/lease 引用，禁止移动赋值。
    AcquisitionBufferPool& operator=(AcquisitionBufferPool&& other) = delete;
    /// Pool 槽位不能复制成第二个所有权域。
    AcquisitionBufferPool(const AcquisitionBufferPool& other) = delete;
    /// Pool 槽位不能复制赋值。
    AcquisitionBufferPool& operator=(const AcquisitionBufferPool& other) = delete;

    /// 在首次派发前为一个 Run 原子预留回退 Buffer，并签发交付凭证。
    /// @param grant_id 非 0 的交付许可 ID。
    /// @param buffer_count 所需固定槽位数，范围必须为 [1, capacity]。
    /// @return 成功时返回持有全部预留槽的 move-only RunDeliveryGrant；参数
    ///         非法或容量不足时返回类型化错误且不保留部分槽。grant 与其签发的
    ///         lease 均不得比当前 Pool 活得更久。
    core::Result<RunDeliveryGrant, AcquisitionBufferPoolError> reserve_delivery(
        std::uint64_t grant_id,
        std::size_t buffer_count) noexcept;

    /// @return 当前容量、预留/租出槽数及累计回退复制次数的值快照。
    Snapshot inspect() const noexcept;

private:
    friend class AcquisitionChunkLease;
    friend class RunDeliveryGrant;

    enum class SlotState {
        Free,
        Reserved,
        Leased
    };

    struct Slot final {
        std::array<ComplexSample, kMaximumContractChunkSamples> samples{};
        std::size_t size{0U};
        std::uint64_t generation{0U};
        SlotState state{SlotState::Free};
    };

    core::Result<AcquisitionChunkLease, BoardError> copy_from_reserved(
        std::size_t slot,
        std::uint64_t generation,
        const std::array<ComplexSample, kMaximumContractChunkSamples>& samples,
        std::size_t size) noexcept;
    void release_reservation(std::size_t slot, std::uint64_t generation) noexcept;
    void release_lease(std::size_t slot, std::uint64_t generation) noexcept;

    std::array<Slot, kMaximumBuffers> slots_{};
    std::size_t capacity_{0U};
    std::uint64_t copy_operations_{0U};
};

}  // namespace vna::board
