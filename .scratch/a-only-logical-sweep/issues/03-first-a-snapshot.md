# 03 — 原子发布第一份单板 A 快照

**What to build:** 同一个公共 A-only 扫描入口在 Mock 成功返回完整入射波和响应波后，生成第一份正式、不可变的完整扫频快照；调用者先看到 Accepted，只有采集真正闭合后才同时看到 A 和 Completed。

**Blocked by:** 02 — 让 A-only 命令走到 Mock 延迟失败终态

**Status:** ready-for-agent

- [ ] Mock 根据本次已接受的实际执行清单返回全部必需小写 a/b Receiver Wave Quantity，而不是使用与 Prepare 无关的私有点数生成结果。
- [ ] 首次派发前已经取得正式 A 输出、candidate metadata 和 commit/abort 所需的固定容量；采集开始后不得为候选结果临时扩张关键资源。
- [ ] 每个正式数据块通过 move-only AcquisitionChunkLease 进入有界 Ingress，Network Observation Builder 是唯一长期所有者；Adapter 不能与 Builder 同时拥有同一 payload。
- [ ] 底软 Buffer 生命周期不能转移时，只能在 callback 返回前复制一次到预留 BufferPool；本票建立该最小契约，复用覆盖与容量耗尽压力场景由工单 09 验收。
- [ ] 只有实际轴、必需观测、全部点覆盖、质量信息和唯一成功 terminal 都闭合后，才能密封 A candidate；成功 terminal 本身不足以发布 A。
- [ ] candidate 从 worker return 到 commit 或 abort 始终由 CandidateCommitLease 持有；提交成功前不可查询，也不能由采集工作直接写入 Store、推进状态或发送 Event。
- [ ] 成功提交在同一个 Store revision 中发布不可变 CompletedSweepBundle、Completed Operation、status/fence 和完成 Event。
- [ ] 测试场景可把 Run 精确设为 350ms；计时原点是 Run Accepted，在 349ms 时仍无 A，在到达 350ms 并完成确定性 pump 后才出现 A 与 Completed。
- [ ] 正式只读事实面能够观察实际频率轴、每项必需 a/b 复数值、质量标志、LogicalSweepId、Manifest 关系、BoardRunId 和 generation，不暴露内部裸 Buffer。
- [ ] 成功终态携带匹配的 A-only completion owner 与 disabled Preview owner；L2 在收到 Store commit receipt 后才将二者各终结一次。
- [ ] A-only 成功不发布 B、Stage 或 C，不派发 Measurement Pipeline 后继，也不制造空 Store handoff。
- [ ] Run Terminal 到 A 可见之间只经过有界的本地完成处理和同步提交，不引入额外 sleep 或模拟扫描延迟。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
