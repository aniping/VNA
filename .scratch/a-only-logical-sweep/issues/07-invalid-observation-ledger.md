# 07 — 拒绝不完整或有歧义的观测账本

**What to build:** 通过同一个公共扫描入口注入缺块、冲突重复、重叠、越界和失败 terminal 时，软件明确判定本次扫描失败，绝不通过补零、忽略冲突或混用旧数据来制造正式 A。

**Blocked by:** 04 — 按实际参数组装乱序多块观测

**Status:** ready-for-agent

- [ ] missing range、gap、conflicting duplicate、overlap、out-of-range 和 terminal-before-complete 场景均不发布 A。
- [ ] Board 成功 terminal 只是必要条件；Manifest 声明的每项观测和每个点范围必须完整、唯一且身份一致才能密封 candidate。
- [ ] failed terminal 不携带 candidate，并使已经可见的 Operation、status/fence 和失败 Event 一致终结。
- [ ] 任何正式数据块都不能被静默丢弃；Ingress 接收失败必须转成类型化失败或善后，而不能继续宣布成功。
- [ ] 不完整数据不得补零，冲突数据不得“最后写入覆盖”，不同 Manifest、Run 或 generation 的数据不得混合。
- [ ] 错误事实保留失败阶段以及相关 Manifest、Run、generation、observation identity 和覆盖摘要。
- [ ] 所有失败场景保持此前不可变 A 完全不变；完整但乱序的成功场景仍须通过，防止实现退化成只接受顺序回调。
- [ ] 测试只使用公开 L2 提交、Mock 场景与 Store 只读事实面，不通过检查 Builder 私有状态证明结果。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
