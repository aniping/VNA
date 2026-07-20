# 09 — 证明回调内存与接收容量安全

**What to build:** 无论底软能够转移 Buffer 所有权，还是在回调返回后立即复用原始内存，正式数据都只由一个明确所有者持有；接收队列或 BufferPool 容量被突破时，扫描显式失败或进入善后，绝不静默丢数据后继续成功。

**Blocked by:** 04 — 按实际参数组装乱序多块观测

**Status:** done

- [x] 正式 chunk payload 通过 move-only lease 恰好转移一次，Adapter 与 Builder 不会同时长期拥有同一数据。
- [x] Mock driver-buffer-reuse 场景在 callback 返回后立即覆盖源内存，最终发布 A 的每个复数值和质量项仍与场景定义完全一致。
- [x] 底软 Buffer 生命周期不能转移时，只允许在 callback 边界复制到预先保留的项目 Buffer；提交热路径和 Run 开始后不得临时扩张关键容量。
- [x] 可预测的 BufferPool 或 Ingress 容量不足在首次单板工作前拒绝，不创建幽灵 Operation 或 Board 调用。
- [x] 回调期间出现的意外 Ingress/BufferPool 契约突破必须接管当前 lease，并使整轮扫描类型化失败或转入善后；不得丢弃 chunk 后发布部分 A。
- [x] 成功、同步拒绝、异步失败和善后路径都证明 Buffer credit、delivery grant、registration 及相关 owner 恰好释放或转交一次。
- [x] 固定上限对 operation、observation、point、chunk、event、ingress 和 buffer 均可检查；测试覆盖边界值与容量耗尽。
- [x] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。

## Comments

- 2026-07-19：公共端到端红灯新增 driver-buffer-reuse、可预测 Ingress/Buffer 不足、运行期 Ingress 超限、运行期 Buffer 回退耗尽和七类固定上限五组测试。红灯因缺少 Profile 容量、Mock 源内存生命周期剧本、复制失败计数、A-only 固定上限与 Store Event 上限而编译失败。
- 2026-07-19：Kernel 以 `2 × ceil(point_count / 64)` 在 O(1) 提交路径计算 a/b 保守块数；Profile 的 Ingress/Buffer 容量为 0 或超过 A-only 公开上限时返回 `InvalidRequest`，合法但不足返回 `AcquisitionResourcesUnavailable`，均发生在 Board execution reservation、ID 分配、Operation/Event 与 Runtime dispatch 之前。其余 Acquisition/Board 资源预留顺序保持工单 05 已封版语义。
- 2026-07-19：Mock 默认继续使用不可转移源数组；`ReuseImmediatelyAfterCallback` 在 `on_chunk()` 返回后立刻写入毒值并累计事实。201 点按 `64 + 64 + 64 + 9` 形成 a/b 共 8 块，正式 A 的 402 个复数值和逐点质量全部与剧本一致，证明 Pool 内唯一复制与后续 move-only lease 不引用已复用源内存。
- 2026-07-19：显式三块剧本模拟 Manifest 只需两块但底软多送一块。Ingress=2/Buffer=3 时第三个 lease 仍被 Sink 接管并记录 `IngressRejected + AbortRunCapacityBreach`；Ingress=3/Buffer=2 时第三次 `copy_fallback()` 失败并产生 `BoardTerminalFailed`。两路均零部分 A，失败后同一 Kernel 的健康扫描成功，execution reservation、grant、registration、Buffer credit 及七项采集 owner 均只终结一次。
- 2026-07-19：Run 同步拒绝矩阵继续要求显式 Prepared discard；discard terminal 前 owner 不释放，terminal 后同一 Kernel 可再次原子预留完整 8 个 Buffer credit、delivery grant、Board registration 和七项采集 owner并成功扫描。由此分别覆盖成功、提交同步拒绝、Run 同步拒绝后 cleanup、Ingress 异步失败和 Buffer 异步失败的资源守恒。
- 2026-07-19：候选前验证为工单 09 定向测试 6/6、MinGW Debug 全量 72/72；`BUILD_TESTING=OFF` 产品构建通过，构建图对 GoogleTest、测试源码及 Runtime/Store 合同测试 hook 引用为 0；`git diff --check` 通过。提交性能继续使用 8 次 warm-up、64 次记录：1 点 median 2200ns/p95 2400ns，201 点 median 2200ns/p95 2500ns；350±50ms 仍只属于 Mock Run 虚拟时间，提交不等待 Run。
- 2026-07-19：代码候选 `4f0cb40` 独立双审核通过。Standards 为 PASS、0 blocking、0 judgement；确认 C++17/MinGW、分层、Doxygen、O(1) 准入、move-only lease、固定 Pool/Ingress、失败分类及工单 10 边界正确。Spec 为 8/8 PASS、0 blocking、0 judgement；逐项确认 callback 返回后 poison、402 个复数值与 402 个质量项、零幽灵容量拒绝、Ingress/Buffer 运行期突破零部分 A、成功/同步拒绝/异步失败/cleanup 资源守恒和七类公开上限。两边独立验证 MinGW Debug 72/72、`BUILD_TESTING=OFF` 产品构建、零测试构建图污染、`git diff --check` 及洁净工作树。
