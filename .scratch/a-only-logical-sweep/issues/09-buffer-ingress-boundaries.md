# 09 — 证明回调内存与接收容量安全

**What to build:** 无论底软能够转移 Buffer 所有权，还是在回调返回后立即复用原始内存，正式数据都只由一个明确所有者持有；接收队列或 BufferPool 容量被突破时，扫描显式失败或进入善后，绝不静默丢数据后继续成功。

**Blocked by:** 04 — 按实际参数组装乱序多块观测

**Status:** ready-for-agent

- [ ] 正式 chunk payload 通过 move-only lease 恰好转移一次，Adapter 与 Builder 不会同时长期拥有同一数据。
- [ ] Mock driver-buffer-reuse 场景在 callback 返回后立即覆盖源内存，最终发布 A 的每个复数值和质量项仍与场景定义完全一致。
- [ ] 底软 Buffer 生命周期不能转移时，只允许在 callback 边界复制到预先保留的项目 Buffer；提交热路径和 Run 开始后不得临时扩张关键容量。
- [ ] 可预测的 BufferPool 或 Ingress 容量不足在首次单板工作前拒绝，不创建幽灵 Operation 或 Board 调用。
- [ ] 回调期间出现的意外 Ingress/BufferPool 契约突破必须接管当前 lease，并使整轮扫描类型化失败或转入善后；不得丢弃 chunk 后发布部分 A。
- [ ] 成功、同步拒绝、异步失败和善后路径都证明 Buffer credit、delivery grant、registration 及相关 owner 恰好释放或转交一次。
- [ ] 固定上限对 operation、observation、point、chunk、event、ingress 和 buffer 均可检查；测试覆盖边界值与容量耗尽。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
