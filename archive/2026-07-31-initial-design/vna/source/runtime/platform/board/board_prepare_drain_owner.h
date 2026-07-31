#pragma once

namespace vna::board {

/// Prepare 终态仍无法证明底软资源已释放时移交给上层的唯一 owner。
///
/// 本 owner 必须与对应 BoardExecutionReservation 和上层采集资源一起保活，
/// 直到后续 Board drain seam 证明真实终态；当前 seam 没有该能力时只能把整组
/// owner 标记为 Quarantined，不能析构后宣称 Drained。
class BoardPrepareDrainOwner final {
public:
    /// 由 Board Adapter 在仍有底软排空义务时签发 owner。
    /// @return 新的有效 move-only drain owner。
    static BoardPrepareDrainOwner issue_for_adapter() noexcept;

    /// 转移唯一 drain owner。
    /// @param other owner 来源；构造后 other 失效。
    BoardPrepareDrainOwner(BoardPrepareDrainOwner&& other) noexcept;
    BoardPrepareDrainOwner& operator=(BoardPrepareDrainOwner&& other) = delete;
    BoardPrepareDrainOwner(const BoardPrepareDrainOwner&) = delete;
    BoardPrepareDrainOwner& operator=(const BoardPrepareDrainOwner&) = delete;

    /// @return 对象仍代表未闭合的 Board Prepare 排空义务时返回 true。
    bool valid() const noexcept { return valid_; }

private:
    explicit BoardPrepareDrainOwner(bool valid) noexcept : valid_(valid) {}
    bool valid_{false};
};

}  // namespace vna::board
