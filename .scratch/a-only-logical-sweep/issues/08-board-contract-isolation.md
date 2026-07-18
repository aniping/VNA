# 08 — 隔离违反协议的单板会话

**What to build:** 当 Mock 单板返回属于错误扫描的数据、重复发送终态或在终态后继续回调时，软件形成稳定、可查询的协议错误并隔离该单板会话，避免后续扫描继续信任已经失序的执行上下文。

**Blocked by:** 04 — 按实际参数组装乱序多块观测

**Status:** ready-for-agent

- [ ] wrong Manifest、wrong prepared execution、wrong BoardRunId、wrong generation、multiple terminal 和 callback-after-terminal 均不发布 A。
- [ ] 每种违约都锁存稳定的 ContractViolation 分类，并保留阶段、相关类型化身份以及 retry/safety 分类；调用者不依赖错误文本解析。
- [ ] 当前 Operation、status/fence 和失败 Event 保持一致，不把协议破坏简化为“没有快照”。
- [ ] 发生违约的 Mock session 进入 isolated fault 状态；后续执行在触发新的单板 Run 前被拒绝，不能把该会话当作健康资源复用。
- [ ] multiple terminal、callback-after-terminal 和其他重复违约不会使 A-only/Preview owner、chunk lease、Board grant、registration 或运行容量被重复终结、释放或归还。
- [ ] 显式关闭并重新打开一个新 Mock session 后可以恢复正常扫描，隔离不能污染无关会话。
- [ ] Board callback 只进行有界身份校验、移动 lease 和投递预留 ingress，不反向调用 L2、Store，也不执行重计算或文件网络工作。
- [ ] 测试只通过 Mock 控制面选择公开故障场景和推进虚拟时间，不手工调用 Prepare/Run 伪造端到端结果。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
