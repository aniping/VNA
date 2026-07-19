# 03 — 原子发布第一份单板 A 快照

**What to build:** 同一个公共 A-only 扫描入口在 Mock 成功返回完整入射波和响应波后，生成第一份正式、不可变的完整扫频快照；调用者先看到 Accepted，只有采集真正闭合后才同时看到 A 和 Completed。

**Blocked by:** 02 — 让 A-only 命令走到 Mock 延迟失败终态

**Status:** ready-for-agent

- [x] Mock 根据本次已接受的实际执行清单返回全部必需小写 a/b Receiver Wave Quantity，而不是使用与 Prepare 无关的私有点数生成结果。
- [x] 首次派发前已经取得正式 A 输出、candidate metadata 和 commit/abort 所需的固定容量；采集开始后不得为候选结果临时扩张关键资源。
- [x] 每个正式数据块通过 move-only AcquisitionChunkLease 进入有界 Ingress，Network Observation Builder 是唯一长期所有者；Adapter 不能与 Builder 同时拥有同一 payload。
- [x] 底软 Buffer 生命周期不能转移时，只能在 callback 返回前复制一次到预留 BufferPool；本票建立该最小契约，复用覆盖与容量耗尽压力场景由工单 09 验收。
- [x] 只有实际轴、必需观测、全部点覆盖、质量信息和唯一成功 terminal 都闭合后，才能密封 A candidate；成功 terminal 本身不足以发布 A。
- [x] candidate 从 worker return 到 commit 或 abort 始终由 CandidateCommitLease 持有；提交成功前不可查询，也不能由采集工作直接写入 Store、推进状态或发送 Event。
- [x] 成功提交在同一个 Store revision 中发布不可变 CompletedSweepBundle、Completed Operation、status/fence 和完成 Event。
- [x] 测试场景可把 Run 精确设为 350ms；计时原点是 Run Accepted，在 349ms 时仍无 A，在到达 350ms 并完成确定性 pump 后才出现 A 与 Completed。
- [x] 正式只读事实面能够观察实际频率轴、每项必需 a/b 复数值、质量标志、LogicalSweepId、Manifest 关系、BoardRunId 和 generation，不暴露内部裸 Buffer。
- [x] 成功终态携带匹配的 A-only completion owner 与 disabled Preview owner；L2 在收到 Store commit receipt 后才将二者各终结一次。
- [x] A-only 成功不发布 B、Stage 或 C，不派发 Measurement Pipeline 后继，也不制造空 Store handoff。
- [x] Run Terminal 到 A 可见之间只经过有界的本地完成处理和同步提交，不引入额外 sleep 或模拟扫描延迟。
- [x] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。

## Comments

- 2026-07-19：以公共 `InstrumentKernel::submit_a_only()`、Mock 虚拟时间和 `InstrumentStore` 只读事实面完成首个红→绿纵切。首条红灯证明缺少 A 查询/完成 Event/成功 owner 终结能力；转绿后在 349ms 保持 Accepted/零 A，在 Run Accepted 后 350ms 到达且完成两次确定性 pump 时，A、Completed Operation、status/fence/Event 使用同一 revision 原子可见。
- 2026-07-19：第二条红灯用 `OmitResponseButComplete` 证明成功 terminal 本身不足以发布 A；有界 Ingress 把 callback 的 move-only chunk 延迟到 Runtime pump 后交给 Manifest 驱动的唯一 Builder，缺少 b 波形成 `IncompleteObservationSet`、零 A，并按失败路径终结 owner。另有 contract test 证明 candidate 只能移动并显式 abort 一次、错误 snapshot receipt 不会消费 completion/Preview owner。
- 2026-07-19：首轮 Standards/Spec 双审核分别发现 2 项阻塞问题。新增红灯后完成最小修复：提交阶段从固定 `AcquisitionBufferPool` 原子预留 a/b 槽，Mock 在 callback 前只复制一次，后续 lease 多次移动保持同一槽地址；Ingress/Builder 拒绝也无条件消费 payload；completion owner 禁止移动赋值覆盖未终结资源；成功测试补查 status 同 revision，并补齐公开 special member Doxygen。Ingress 静态深度从 64 收紧为当前 Manifest 最大必需观测数，实际 A-only 准入为 2。第二轮 Standards 又发现 Engine 最外层早退未接管异常 chunk；新增假板在 `RunAccepted` 前违规内联交付的公共 seam 红灯后，Engine 现在也先接管再拒绝。完整测试通过 47/47；`BUILD_TESTING=OFF` 产品构建通过且生成图无 GTest/测试源引用。提交性能 8 次 warm-up、64 次记录：1 点 median 1800ns/p95 2400ns，201 点 median 1800ns/p95 3400ns，仍无按扫描点数执行的提交工作。等待最终 Standards/Spec 双审核。
- 2026-07-19：代码候选 `903abe3` 最终双审核通过：Standards 为 0 blocking、0 judgement；Spec 为 13/13 PASS、0 blocking。两边均独立确认 47/47、`BUILD_TESTING=OFF` 和 `git diff --check`，且工单 04/06/09 边界保持不变。
