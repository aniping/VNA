# 04 — 按实际参数组装乱序多块观测

**What to build:** Mock 单板像真实底软一样，在接受 Run 后约 350ms 的窗口内分批、甚至乱序交付多项接收机观测；软件按照实际执行参数和数据覆盖范围重组完整结果，并保证连续扫描产生彼此独立的不可变历史快照。

**Blocked by:** 03 — 原子发布第一份单板 A 快照

**Status:** ready-for-agent

- [x] Prepared Execution Manifest 是实际点数、实际频率轴和有界 required observation map 的唯一执行权威，Mock 输出必须从该 Manifest 派生。
- [x] 每个正式数据块的身份足以区分 Manifest、prepared execution、Board run、generation、source state、receiver path、wave、sequence 和点范围。
- [x] Mock 场景先为本轮确定一个 300～400ms 的 run_duration；各数据块的确定性 offset 均落在 Run Accepted 后的 0～run_duration 之间且早于 terminal，唯一 Run Terminal 在 run_duration 到达。场景可以指定乱序，测试不依赖真实随机数或墙钟时间。
- [x] 完整且互不重叠的数据块即使乱序到达也能成功组装，回调顺序不得成为正式数据位置。
- [x] Builder 由 Manifest 的观测集合和覆盖范围驱动，不硬编码固定 a/b 数组位置或单板型号分支。
- [x] A 保存实际轴、Typed Quality Plane 以及单元素 BoardRunEvidence；evidence 包含 Manifest、session/capability、BoardRunId、generation、coverage、sequence 和唯一 terminal 账本。
- [x] 同一个公共入口连续成功提交两次扫描时产生不同 Snapshot identity，第二次提交不修改第一份 A 的数值、质量或来源信息。
- [x] 端到端测试只通过 Mock 控制面选择场景和推进虚拟时间，不直接调用 Prepare/Run，也不检查 Builder 私有数组或队列。
- [x] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。

## Comments

- 2026-07-19：首条公共端到端红灯从 `InstrumentKernel::submit_a_only()` 提交 6 点扫描，通过 Mock 场景声明四个不同 offset、位置乱序的数据块，并要求 Store 暴露 typed observation identity、逐点质量、callback 顺序证据和两次扫描的独立历史；红灯只因 `run_duration`、chunk plan、source/path identity 与 evidence ledger 接口尚不存在而失败。
- 2026-07-19：转绿后 Mock 在 300～400ms 窗口内按虚拟事件时间稳定排序到期项，同一 `advance()` 跨越多个 offset 也不改变 callback sequence；默认计划只从 Manifest 的 required observation map 和实际点数派生。201 点 a/b 上界测试证明每项按 `64 + 64 + 64 + 9` 拆分，首次派发前统一准入 8 个 Pool/Ingress 槽，terminal 固定在 Run Accepted 后 350ms，过程不使用随机数、线程、sleep 或 wall clock。
- 2026-07-19：`NetworkObservationBuilder` 以 `source state + receiver path + wave` 查找 Manifest 项，按不重叠点范围接管 move-only lease，并独立保存 callback ledger；Store 按 `point_begin` 定位复制值和质量，因此数据位置不受回调顺序影响。连续两次公共提交生成不同 CompletedSweepId/LogicalSweepId，第二次发布后第一份数值、质量和 BoardRunEvidence 仍可按原 Operation 查询。
- 2026-07-19：候选前验证为 MinGW Debug 49/49 通过；`BUILD_TESTING=OFF` 产品构建通过且 build graph 对 GoogleTest/测试源引用为 0。提交性能使用 8 次 warm-up、64 次记录：1 点 median 2000ns/p95 2200ns，201 点 median 2000ns/p95 2400ns；Run 的 350ms 模拟耗时不进入 submit 路径。等待 Standards/Spec 双审核。
