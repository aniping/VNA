#pragma once

#include "runtime/core/base/strong_id.h"

namespace vna::board {

class BoardExecutionPort;

/// 标识一项由具体 Board Adapter 预占的 Prepare/Run 执行槽。
using BoardExecutionReservationId =
    core::StrongId<struct BoardExecutionReservationIdTag>;

/// 在首次 Runtime dispatch 前取得的单板执行容量 owner。
///
/// 租约覆盖同一次逻辑扫描的 Prepare/Run 排队容量及其 callback registration
/// 容量，并按 Reserved→Preparing→Prepared→Running→Terminal 一次性消费。
/// 对象为 move-only；析构请求签发它的 BoardExecutionPort 归还槽位，但若仍有
/// Accepted callback 或已 Quarantined，Adapter 必须保留容量而不能危险复用。
class BoardExecutionReservation final {
public:
    /// 转移 Adapter execution 槽的唯一所有权。
    /// @param other 租约来源；构造后 other 失效且不会归还该槽。
    BoardExecutionReservation(BoardExecutionReservation&& other) noexcept;
    /// 已持有的 Adapter 槽可能仍绑定 Accepted callback，因此禁止 move 赋值
    /// 覆盖；所有权只能通过 move 构造单向转移。
    BoardExecutionReservation& operator=(BoardExecutionReservation&& other) = delete;
    BoardExecutionReservation(const BoardExecutionReservation&) = delete;
    BoardExecutionReservation& operator=(const BoardExecutionReservation&) = delete;
    /// 归还尚未释放的 Adapter 执行槽；不代替已接受调用的 terminal 或 abort。
    /// @note 签发租约的 BoardExecutionPort 必须比本对象活得更久。若违反契约在
    ///       Accepted terminal 前析构，Adapter 可以保留/隔离槽位而不能危险复用。
    ~BoardExecutionReservation();

    /// @return 租约仍绑定非 0 Adapter 槽位时返回 true。
    bool valid() const noexcept;
    /// @return Adapter 签发的槽位身份；仅用于同一 BoardExecutionPort 内校验。
    BoardExecutionReservationId id() const noexcept { return id_; }

private:
    friend class BoardExecutionPort;
    BoardExecutionReservation(
        BoardExecutionPort& owner,
        BoardExecutionReservationId id) noexcept;
    void release() noexcept;
    void invalidate() noexcept;

    BoardExecutionPort* owner_{nullptr};
    BoardExecutionReservationId id_{};
};

}  // namespace vna::board
