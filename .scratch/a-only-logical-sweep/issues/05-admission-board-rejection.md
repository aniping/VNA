# 05 — 全有或全无地处理准入与单板拒绝

**What to build:** 当扫描在容量检查、版本检查、初始提交、Prepare、实际参数确认或 Run 提交阶段被拒绝时，调用者得到与失败阶段一致的结果；系统不会留下幽灵任务、启动不该发生的扫描或遗失任何已预留资源。

**Blocked by:** 02 — 让 A-only 命令走到 Mock 延迟失败终态

**Status:** ready-for-agent

- [x] Runtime、Store、Buffer、Ingress 或 Board 容量不足，以及授权或 revision 失效时同步拒绝，且没有可见 Operation、Board callback 或 Event。
- [x] 初始 Store 提交失败释放完整本地准入所有权，不派发 Runtime，也不调用 Board。
- [x] Accepted 已经可见后的内部派发契约异常不能伪装成“从未接受”，必须使用预留终态容量形成一个一致的 Failed Operation。
- [x] Prepare 同步拒绝原样返还全部 move-only 输入，不产生 Prepare callback、Run 或 A。
- [x] 实际 Manifest 过期、身份不匹配或超出保守 envelope 时，在 Run 前失败；精确收窄能力只能在采集层本地消费，不得扩容、换板或反向调用上层与 Store。
- [x] Run 同步拒绝返还 prepared token、authorization、delivery grant 和 sink registration，不产生任何 Run callback 或 A。
- [x] 本票中的已接受 Prepare/Run 异步失败必须能够得到真实 cleanup terminal，并只在该 terminal 后释放资源和提交 Failed；长期无 terminal 的善后移交由工单 10 验收。
- [x] 类型化错误保留失败阶段、相关身份及 retry/safety 分类；调用者不解析诊断字符串决定行为。
- [x] 所有失败场景证明每个 owner 只归还、消费或转交一次，不留下可被后续请求误用的残余能力。
- [x] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。

## Comments

- 2026-07-19：首条 RED 证明 `AcquisitionAdmissionPool::narrow_to()` 错把观测条目数当成 chunk 数，65 点单观测在只预留 1 块时仍会通过；修复后按每项观测的 64 点块上限累加真实块数，并使用 `source state + receiver path + wave` 完整身份判重。同一 wave、不同激励状态不再被误判为重复观测。
- 2026-07-19：公共 A-only seam 新增 Prepare/Run 同步拒绝、Prepare 接受后异步失败、Runtime/Store 容量不足、revision 冲突以及三类非法 Manifest 测试。同步拒绝零 callback/零 A；PrepareFailed 在 25ms cleanup terminal 前保持 Accepted 和全部 owner，terminal 后才由后续 Runtime/L2 pump 原子提交 Failed 并恰好一次释放资源。
- 2026-07-19：`AOnlySweepRequest::expected_capability_revision` 为 0 时由 Kernel 冻结当前 cut，非 0 时必须精确匹配；冲突在 Board execution、Acquisition、Buffer、Runtime 和 Store 预留之前返回 `RevisionConflict`。Mock 可确定性返回 stale capability、mismatched session 或 expanded point envelope，Acquisition 都在 Run 前本地消费 one-shot 精确收窄并失败，不调用上层或 Store、不扩容和不换板。
- 2026-07-19：失败 Event 现包含稳定 phase/reason、Prepare/Prepared/Run/generation、BoardErrc（存在时）、retry class 和执行生命周期 safety impact。safety 只表示 NoRunAccepted、RunTerminalObserved 或 ResourceIsolationRequired，文档明确禁止把它解释为真实 RF-off/互锁证明；长期缺失 terminal、完整 Drain supervisor 和真实单板安全仍由工单 10/生产门禁负责。
- 2026-07-19：候选前验证为 MinGW Debug 57/57 通过；`BUILD_TESTING=OFF` 产品构建通过且 build graph 对 GoogleTest/测试源引用为 0。提交性能使用 8 次 warm-up、64 次记录：1 点 median 2050ns/p95 2300ns，201 点 median 2000ns/p95 2300ns；350±50ms 仍只属于 Mock Run，不进入 submit 路径。等待 Standards/Spec 双审核。
