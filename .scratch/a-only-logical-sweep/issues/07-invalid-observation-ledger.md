# 07 — 拒绝不完整或有歧义的观测账本

**What to build:** 通过同一个公共扫描入口注入缺块、冲突重复、重叠、越界和失败 terminal 时，软件明确判定本次扫描失败，绝不通过补零、忽略冲突或混用旧数据来制造正式 A。

**Blocked by:** 04 — 按实际参数组装乱序多块观测

**Status:** ready-for-agent

- [x] missing range、gap、conflicting duplicate、overlap、out-of-range 和 terminal-before-complete 场景均不发布 A。
- [x] Board 成功 terminal 只是必要条件；Manifest 声明的每项观测和每个点范围必须完整、唯一且身份一致才能密封 candidate。
- [x] failed terminal 不携带 candidate，并使已经可见的 Operation、status/fence 和失败 Event 一致终结。
- [x] 任何正式数据块都不能被静默丢弃；Ingress 接收失败必须转成类型化失败或善后，而不能继续宣布成功。
- [x] 不完整数据不得补零，冲突数据不得“最后写入覆盖”，不同 Manifest、Run 或 generation 的数据不得混合。
- [x] 错误事实保留失败阶段以及相关 Manifest、Run、generation、observation identity 和覆盖摘要。
- [x] 所有失败场景保持此前不可变 A 完全不变；完整但乱序的成功场景仍须通过，防止实现退化成只接受顺序回调。
- [x] 测试只使用公开 L2 提交、Mock 场景与 Store 只读事实面，不通过检查 Builder 私有状态证明结果。
- [x] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。

## Comments

- 2026-07-19：首个公共端到端红灯从 `submit_a_only()` 依次注入整项缺失、内部 gap、冲突重复、部分 overlap、越界、Completed 早于计划覆盖、failed terminal 和无效 payload；红灯因 Mock 缺少后两类确定性剧本、失败 Event 没有观测账本证据且错误码无法区分冲突/重叠/越界/Ingress 拒绝而编译失败。
- 2026-07-19：`NetworkObservationError` 现保存 Manifest/Prepared/Run/generation、类型化必需观测、可选拒绝块、期望观测数/完整观测数/期望点/已接受唯一点/首段缺口和 terminal 摘要。Builder 首错锁存且后续 terminal 只补齐终态证据；完整性计算只在有界 Run 完成/失败路径执行，不进入 `submit_a_only()` 热路径。
- 2026-07-19：Mock 显式计划可确定性表达 gap、重复、overlap 和 Manifest 范围错误；可限制 Completed terminal 前的实际交付数，并可交付 moved-from invalid lease 验证 Ingress 拒绝。冲突/重叠/越界块先被合法 Ingress 接收，故 Board 仍可报告 Completed，随后 Builder 拒绝再次证明 terminal 不是完整性替代品；Mock-specific 分支没有进入 L2/L4/L5。
- 2026-07-19：端到端矩阵逐项验证 Failed Operation、status、fence、Event 同 revision、零新 A、类型化阶段/retry/safety/身份/覆盖事实，并在每次失败后复查既有 A 的 ID、revision、LogicalSweepId、轴、a/b 首尾值、逐点质量及 Manifest/Run 来源不变；最后一轮乱序完整计划仍成功发布，证明实现没有退化成按 callback 顺序组装。
- 2026-07-19：候选前验证为 MinGW Debug 65/65 通过；`BUILD_TESTING=OFF` 产品构建通过且 build graph 对 GoogleTest、测试源码及 Runtime/Store hook 宏引用为 0；`git diff --check` 通过。提交性能使用 8 次 warm-up、64 次记录：1 点 median 2400ns/p95 3600ns，201 点 median 2600ns/p95 3500ns；等待 Standards/Spec 双审核。
- 2026-07-19：首轮独立 Spec 审核 9/9 PASS、0 blocking、0 judgement；首轮 Standards 审核发现 1 个 blocking、0 judgement：新增公开头 `network_observation_error.h` 未登记到 Runtime CMake 的显式头文件清单。现已补齐该登记，未改变运行时行为，等待重新验证与双审核。
- 2026-07-19：CMake 登记修复后，工单 07 定向测试 1/1、MinGW Debug 全量测试 65/65、`BUILD_TESTING=OFF` 产品构建均通过；产品构建图对 GoogleTest、测试源码及 Runtime/Store hook 宏引用为 0，`git diff --check` 通过，提交后重新发起完整范围双审核。
