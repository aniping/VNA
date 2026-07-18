# 06 — 在最终保存失败后原子回滚

**What to build:** 当完整扫描数据已经形成、但最终正式发布被 Store 拒绝时，调用者只能看到一致的失败任务；半份 A、孤立完成通知或错误的 Completed 状态都不能逃逸，历史成功结果保持不变。

**Blocked by:** 03 — 原子发布第一份单板 A 快照

**Status:** ready-for-agent

- [ ] 可对成功 A candidate 的最终提交注入 validation 或 write failure，并证明 candidate 在提交前仍不可见。
- [ ] 最终提交失败时，A、完成 Event、Completed 状态和完成 fence/status 全部不可见，不允许只成功其中一部分。
- [ ] 同一个有界控制回合使用初始 Accepted 时安装的终态预留完成 state-only Failed 提交，使 Operation、failure Event 和失败 status/fence 在同一 revision 一致可见。
- [ ] CandidateCommitLease 沿唯一 abort 路径消费，候选 Buffer 和父关系不会泄漏，也不会进入正式查询面。
- [ ] A-only completion owner 与 disabled Preview owner 在失败事实的 commit receipt 前继续由 L2 持有，随后各自恰好失败终结一次。
- [ ] 提交失败不会修改任何既有不可变 A；已有只读句柄仍能观察原来的值、质量和来源信息。
- [ ] 只有明确的 Store integrity fault 才允许进入 fail-stop；普通容量、校验或写入拒绝不得让 Operation 永久停留在 Pending/Publishing。
- [ ] Store 的事务类型保持有 schema 的领域事实，不能退化成任意 key/value，也不能包含 Runtime、Board 或 Preview 的可执行能力。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
