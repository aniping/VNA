# 08 — 隔离违反协议的单板会话

**What to build:** 当 Mock 单板返回属于错误扫描的数据、重复发送终态或在终态后继续回调时，软件形成稳定、可查询的协议错误并隔离该单板会话，避免后续扫描继续信任已经失序的执行上下文。

**Blocked by:** 04 — 按实际参数组装乱序多块观测

**Status:** ready-for-agent

- [x] wrong Manifest、wrong prepared execution、wrong BoardRunId、wrong generation、multiple terminal 和 callback-after-terminal 均不发布 A。
- [x] 每种违约都锁存稳定的 ContractViolation 分类，并保留阶段、相关类型化身份以及 retry/safety 分类；调用者不依赖错误文本解析。
- [x] 当前 Operation、status/fence 和失败 Event 保持一致，不把协议破坏简化为“没有快照”。
- [x] 发生违约的 Mock session 进入 isolated fault 状态；后续执行在触发新的单板 Run 前被拒绝，不能把该会话当作健康资源复用。
- [x] multiple terminal、callback-after-terminal 和其他重复违约不会使 A-only/Preview owner、chunk lease、Board grant、registration 或运行容量被重复终结、释放或归还。
- [x] 显式关闭并重新打开一个新 Mock session 后可以恢复正常扫描，隔离不能污染无关会话。
- [x] Board callback 只进行有界身份校验、移动 lease 和投递预留 ingress，不反向调用 L2、Store，也不执行重计算或文件网络工作。
- [x] 测试只通过 Mock 控制面选择公开故障场景和推进虚拟时间，不手工调用 Prepare/Run 伪造端到端结果。
- [x] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。

## Comments

- 2026-07-19：首个公共端到端红灯从 `submit_a_only()` 分别注入 wrong Manifest、wrong PreparedExecution、wrong BoardRunId、wrong generation、multiple terminal 和 terminal 后继续交付 chunk；测试要求同 revision Failed 事实、稳定首错证据、旧会话隔离、隔离后新 Run 前同步拒绝，以及关闭重开后新会话恢复。红灯因缺少 Mock 故障枚举、类型化 `BoardContractViolation`、会话隔离查询/计数和 `BoardSessionIsolated` 提交拒绝分类而编译失败。
- 2026-07-19：`BoardContractViolation` 以固定大小值保存首个违约种类、callback 种类、期望/实际 Manifest、PreparedExecution、BoardRunId、generation 和 terminal callback 计数。Board callback 先无条件接管 chunk lease，再只做有界身份比较和锁存；`AcquisitionEngine` 在 callback 返回后的 Runtime 步骤调用通用 `isolate_contract_violation()`，随后由 Store 把 Failed Operation、status/fence 和失败 Event 原子写入同一 revision。
- 2026-07-19：Mock session 只允许 `Healthy → IsolatedContractViolation` 一次；隔离后 `reserve_execution()` 返回 ContractViolation，L2 映射为 `BoardSessionIsolated/AfterRecovery/NoRunAccepted`，不会创建幽灵 Operation 或触发新 Prepare/Run。Provider 仅在成功 open 后推进非零 SessionId；销毁旧会话并重开得到不同 ID，健康扫描成功，隔离状态不跨会话传播。
- 2026-07-19：multiple terminal 和 terminal 后 chunk 都在同一次 `advance()` 内完成，Engine/sink 仍存活；测试逐场景确认唯一失败 Event、零 A、execution reservation 只释放一次、隔离只锁存一次，错误 chunk lease 在 callback 拒绝路径由唯一 owner 析构。测试不读取 Engine/Builder 私有状态，也未实现工单 09 的 driver-buffer-reuse 或容量压力矩阵。
- 2026-07-19：候选前验证为工单 08 定向测试 1/1、MinGW Debug 全量 66/66；`BUILD_TESTING=OFF` 产品构建通过且 build graph 对 GoogleTest、测试源码及 Runtime/Store hook 宏引用为 0；`git diff --check` 通过。提交性能使用既有 8 次 warm-up、64 次记录：1 点 median 2300ns/p95 2800ns，201 点 median 2300ns/p95 3000ns；等待 Standards/Spec 双审核。
- 2026-07-19：代码候选 `1adea49` 首轮双审核通过：Standards 为 0 blocking、0 judgement；Spec 为 9/9 PASS、0 blocking、0 judgement。两边均独立确认 MinGW Debug 66/66、`BUILD_TESTING=OFF` 产品构建、零 GoogleTest/测试源码/Runtime+Store hook 构建图引用、`git diff --check` 和干净工作树；Spec 另确认 move-only lease、唯一 terminal、幂等隔离、RAII reservation、A-only/Preview 首次终结及 Runtime 单终态共同构成充分的资源守恒证据，工单 09–10 边界保持不变。
