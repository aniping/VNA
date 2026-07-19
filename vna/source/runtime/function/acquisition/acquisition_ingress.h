#pragma once

#include "runtime/platform/board/board_port.h"

#include <array>
#include <cstddef>
#include <optional>

namespace vna::acquisition {

/// 单次 Run 允许的最大正式 chunk 队列深度；构造实例可选择更小固定容量。
constexpr std::size_t kMaximumAcquisitionIngressChunks =
    board::kMaximumRunChunks;

/// Board callback 与 Runtime 驱动 Builder 之间的固定容量 move-only Ingress。
///
/// push() 只做有界校验与所有权移动，不执行轴生成、Store 写入或数值处理；pop()
/// 把唯一 payload owner 交给 NetworkObservationBuilder。当前 Mock 组合串行调用
/// callback 与 pump；真实底软线程 ABI 未签字前本类型不宣称跨线程无锁安全。
class AcquisitionIngress final {
public:
    /// @param capacity 首次派发前准入的 chunk 数，范围必须为
    ///        [1, kMaximumAcquisitionIngressChunks]；越界创建无效 Ingress。
    explicit AcquisitionIngress(std::size_t capacity) noexcept;
    /// Ingress 地址被 Board callback 注册间接依赖，禁止移动。
    AcquisitionIngress(AcquisitionIngress&& other) = delete;
    /// Ingress 地址被 Board callback 注册间接依赖，禁止移动赋值。
    AcquisitionIngress& operator=(AcquisitionIngress&& other) = delete;
    /// 队列中的 move-only chunk owner 不能复制。
    AcquisitionIngress(const AcquisitionIngress&) = delete;
    /// 队列中的 move-only chunk owner 不能复制赋值。
    AcquisitionIngress& operator=(const AcquisitionIngress&) = delete;

    /// @return 构造容量有效时返回 true。
    bool valid() const noexcept { return capacity_ > 0U; }

    /// 从 Board callback 接管一个正式 chunk。
    /// @param chunk payload 所有权转移到队列；即使拒绝，调用结束后 Adapter 也
    ///        不再拥有输入对象。
    /// @return 入队成功为 Accepted；无效 payload 或容量承诺被突破时分别返回
    ///         ProtocolViolation 或 CapacityBreach，队列不动态扩张。
    board::ChunkIngressDisposition push(
        board::ReceiverObservationChunk&& chunk) noexcept;

    /// 取出最早进入队列的正式 chunk。
    /// @return 非空时转移唯一 payload owner；空队列返回 std::nullopt。
    std::optional<board::ReceiverObservationChunk> pop() noexcept;

    /// @return 当前由 Ingress 临时拥有、尚未交给 Builder 的 chunk 数。
    std::size_t size() const noexcept { return size_; }

private:
    std::array<
        std::optional<board::ReceiverObservationChunk>,
        kMaximumAcquisitionIngressChunks> slots_{};
    std::size_t capacity_{0U};
    std::size_t head_{0U};
    std::size_t tail_{0U};
    std::size_t size_{0U};
};

}  // namespace vna::acquisition
