# 05 — 全有或全无地处理准入与单板拒绝

**What to build:** 当扫描在容量检查、版本检查、初始提交、Prepare、实际参数确认或 Run 提交阶段被拒绝时，调用者得到与失败阶段一致的结果；系统不会留下幽灵任务、启动不该发生的扫描或遗失任何已预留资源。

**Blocked by:** 02 — 让 A-only 命令走到 Mock 延迟失败终态

**Status:** ready-for-agent

- [ ] Runtime、Store、Buffer、Ingress 或 Board 容量不足，以及授权或 revision 失效时同步拒绝，且没有可见 Operation、Board callback 或 Event。
- [ ] 初始 Store 提交失败释放完整本地准入所有权，不派发 Runtime，也不调用 Board。
- [ ] Accepted 已经可见后的内部派发契约异常不能伪装成“从未接受”，必须使用预留终态容量形成一个一致的 Failed Operation。
- [ ] Prepare 同步拒绝原样返还全部 move-only 输入，不产生 Prepare callback、Run 或 A。
- [ ] 实际 Manifest 过期、身份不匹配或超出保守 envelope 时，在 Run 前失败；精确收窄能力只能在采集层本地消费，不得扩容、换板或反向调用上层与 Store。
- [ ] Run 同步拒绝返还 prepared token、authorization、delivery grant 和 sink registration，不产生任何 Run callback 或 A。
- [ ] 本票中的已接受 Prepare/Run 异步失败必须能够得到真实 cleanup terminal，并只在该 terminal 后释放资源和提交 Failed；长期无 terminal 的善后移交由工单 10 验收。
- [ ] 类型化错误保留失败阶段、相关身份及 retry/safety 分类；调用者不解析诊断字符串决定行为。
- [ ] 所有失败场景证明每个 owner 只归还、消费或转交一次，不留下可被后续请求误用的残余能力。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
