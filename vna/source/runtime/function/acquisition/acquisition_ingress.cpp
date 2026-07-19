#include "runtime/function/acquisition/acquisition_ingress.h"

#include <utility>

namespace vna::acquisition {

AcquisitionIngress::AcquisitionIngress(std::size_t capacity) noexcept
    : capacity_(capacity > 0U && capacity <= slots_.size() ? capacity : 0U) {}

board::ChunkIngressDisposition AcquisitionIngress::push(
    board::ReceiverObservationChunk&& chunk) noexcept {
    // rvalue-reference 绑定本身不会移动。先建立本地 owner，确保所有拒绝分支
    // 都在本函数返回前释放 payload，调用方永远不能重新使用已交付数据块。
    auto owned = std::move(chunk);
    if (!owned.payload.valid()) {
        return board::ChunkIngressDisposition::AbortRunProtocolViolation;
    }
    if (!valid() || size_ >= capacity_) {
        return board::ChunkIngressDisposition::AbortRunCapacityBreach;
    }
    slots_[tail_].emplace(std::move(owned));
    tail_ = (tail_ + 1U) % capacity_;
    ++size_;
    return board::ChunkIngressDisposition::Accepted;
}

std::optional<board::ReceiverObservationChunk> AcquisitionIngress::pop() noexcept {
    if (size_ == 0U) {
        return std::nullopt;
    }
    auto result = std::optional<board::ReceiverObservationChunk>{
        std::move(*slots_[head_])};
    slots_[head_].reset();
    head_ = (head_ + 1U) % capacity_;
    --size_;
    return result;
}

}  // namespace vna::acquisition
