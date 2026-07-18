# 10 — 保全卡住扫描的善后所有权

**What to build:** Mock 单板已经接受运行、但在执行上下文给定期限内一直不返回真实终态时，软件把完整采集义务转交给一个具名善后任务；相关运行槽、Buffer 和单板能力在真正善后终结前都不能被下一次扫描复用。

**Blocked by:** 03 — 原子发布第一份单板 A 快照

**Status:** ready-for-agent

- [ ] Mock run_duration 与 ExecutionContext deadline 是两个独立参数：350±50ms 只表示 Run Accepted 到 Run Terminal；正常场景的 deadline 必须独立设置且晚于其 terminal，400ms 不是 timeout、安全阈值、abort SLA 或 RF-off 证明。
- [ ] 可重复的卡住场景在 Run Accepted 后超过测试独立提供的有界 deadline 仍不产生 terminal，并触发确定性的善后移交。
- [ ] deadline 到达后的有界状态转换产生唯一 DrainId，并把尚未终结的义务作为完整 AcquisitionDrainOwner 移交。
- [ ] 善后 owner 同时持有仍存活的 Board token/call/sink、Manifest、Builder、Buffer/Ingress 容量、运行完成注册、A-only completion owner、disabled Preview owner，以及 Manifest 精确收窄后形成且仍受本轮约束的 run resource ownership；此时 ExactFinalizationCapability 已经消费完毕，不能再次出现。
- [ ] 具名 Drain 首次可见时同步安装它自己的 LifecycleTerminalReservation；父 Operation 的失败/善后事实与 Drain 事实通过已预留容量一致可见，Draining 不发布 A，也不制造 Completed。
- [ ] Runtime lane 和所有相关容量在唯一 Drain terminal 前不可复用；容量检查能够观察这一点。
- [ ] 进入 Draining 后迟到的成功数据不能晋升为 A，所有迟到 owner 只能沿善后协议消费或隔离。
- [ ] Drain terminal 到达后全部资源恰好释放一次，重复或错误 Drain terminal 被报告为类型化合同错误。
- [ ] 测试使用虚拟时间和确定性 pump，不使用 sleep，并明确只证明软件所有权守恒，不宣称 abort、RF-off、readback 或物理 RF 安全已经实现。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
