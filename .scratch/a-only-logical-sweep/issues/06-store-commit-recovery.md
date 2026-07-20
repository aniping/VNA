# 06 — 在最终保存失败后原子回滚

**What to build:** 当完整扫描数据已经形成、但最终正式发布被 Store 拒绝时，调用者只能看到一致的失败任务；半份 A、孤立完成通知或错误的 Completed 状态都不能逃逸，历史成功结果保持不变。

**Blocked by:** 03 — 原子发布第一份单板 A 快照

**Status:** ready-for-agent

- [x] 可对成功 A candidate 的最终提交注入 validation 或 write failure，并证明 candidate 在提交前仍不可见。
- [x] 最终提交失败时，A、完成 Event、Completed 状态和完成 fence/status 全部不可见，不允许只成功其中一部分。
- [x] 同一个有界控制回合使用初始 Accepted 时安装的终态预留完成 state-only Failed 提交，使 Operation、failure Event 和失败 status/fence 在同一 revision 一致可见。
- [x] CandidateCommitLease 沿唯一 abort 路径消费，候选 Buffer 和父关系不会泄漏，也不会进入正式查询面。
- [x] A-only completion owner 与 disabled Preview owner 在失败事实的 commit receipt 前继续由 L2 持有，随后各自恰好失败终结一次。
- [x] 提交失败不会修改任何既有不可变 A；已有只读句柄仍能观察原来的值、质量和来源信息。
- [x] 只有明确的 Store integrity fault 才允许进入 fail-stop；普通容量、校验或写入拒绝不得让 Operation 永久停留在 Pending/Publishing。
- [x] Store 的事务类型保持有 schema 的领域事实，不能退化成任意 key/value，也不能包含 Runtime、Board 或 Preview 的可执行能力。
- [x] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。

## Comments

- 2026-07-19：首条独立 `StoreCommitRecoveryContract` 红灯从公共 `submit_a_only()` 推进到 Runtime 已持有成功 completion、但 candidate 仍不可查询的边界，要求分别注入 typed validation/write rejection；红灯只因 Store 没有测试构建专属 fault seam 与稳定拒绝码而编译失败。
- 2026-07-19：Store 只在 `BUILD_TESTING=ON` 保存一次性故障字段。validation 在正式 bundle 构造前拒绝，write 在本地 staging 完成后、切换 revision 前拒绝；两者都原样返还完整 `CandidateCommitLease`，不改变 revision、Catalog、Operation、Event、status 或 fence。Kernel 在同一个 completion 回合使用既有终态预留提交 Failed，receipt 后依次 abort candidate、失败终结 A-only/disabled Preview owner，再释放槽位。
- 2026-07-19：validation 回滚后立即成功提交下一次扫描，证明 candidate payload、Board execution 与上层 owner 可复用且失败终结恰好一次。write 回滚前先发布并保存一份历史 A，失败后再次查询的 ID、revision、复数值、质量和 BoardRunId 均保持不变，正式 A 数量没有增加。
- 2026-07-19：第二条红灯要求只有显式 `StoreErrc::IntegrityFault` 能进入 typed `StoreFailStop`。修复后 state-only Failed 缺少有效首次 commit receipt 时，Kernel 保留 candidate/owner 和原 Accepted 事实，公开记录触发 Operation/StoreError，并在读取 Board capability 或取得任何新容量前以 `InstrumentFailStop` 同步拒绝后续提交；validation/write 普通拒绝的完整性状态保持 Healthy。
- 2026-07-19：候选前验证为 MinGW Debug 63/63 通过；`BUILD_TESTING=OFF` 产品构建通过且 build graph 对 GoogleTest、测试源码、Runtime/Store fault hook 宏引用为 0；`git diff --check` 通过。提交性能使用 8 次 warm-up、64 次记录：1 点 median 2100ns/p95 2200ns，201 点 median 2100ns/p95 2200ns；最终 Store commit/recovery 不进入 submit 计时路径。等待 Standards/Spec 双审核。
- 2026-07-19：首轮 Standards/Spec 双审核未通过。两边都指出 Store 的公开提交接口没有完整写明 validation/write rejection、候选所有权返还及零正式事实变更；Standards 另发现 Kernel 只检查 state/disposition，可能把错误 OperationId 或零 revision 的伪成功失败回执当作有效证据并释放 candidate/owner。
- 2026-07-19：审核修复补齐 `commit_completed_sweep()`、`commit_acquisition_failed()` 与 `submit_a_only()` 的公开 Doxygen。Kernel 现在同时核验 receipt 的 OperationId、非零 revision、Failed/disposition，以及 Operation、fence、status、Event 的同一 Operation/revision 事实；任一不一致都保留 candidate/owner 并进入 typed `StoreFailStop`。新增公共流程回归测试，用测试构建专属 seam 返回错误 OperationId/零 revision 的伪成功回执，证明 Accepted 事实不变、无 fence/Event、Acquisition owner 未释放。
- 2026-07-19：修复候选验证为 MinGW Debug 64/64 通过，其中 `StoreCommitRecoveryContract` 4/4；`BUILD_TESTING=OFF` 产品构建通过，build graph 对 GoogleTest、测试源码、Runtime/Store fault hook 宏引用为 0；`git diff --check` 通过。等待修复后的 Standards/Spec 双复审。
- 2026-07-19：第二轮 Spec 复审为 9/9 PASS、0 blocking、0 judgement；Standards 确认首轮两个阻塞与失效安全均已闭合，但仍因测试访问器的类级 Doxygen 只描述 A 发布拒绝、未覆盖终态完整性故障和畸形回执职责而判定 FAIL。
- 2026-07-19：第二轮 Standards 文档修复只更新测试访问器职责契约，明确其覆盖一次性正式 A 发布拒绝、终态提交完整性故障和畸形回执，并注明关闭测试的产品构建不包含该类型及入口。定向重新编译通过，`StoreCommitRecoveryContract` 4/4、MinGW Debug 全量 64/64 通过；等待最终 Standards/Spec 双复审。
