# 04 — 按实际参数组装乱序多块观测

**What to build:** Mock 单板像真实底软一样，在接受 Run 后约 350ms 的窗口内分批、甚至乱序交付多项接收机观测；软件按照实际执行参数和数据覆盖范围重组完整结果，并保证连续扫描产生彼此独立的不可变历史快照。

**Blocked by:** 03 — 原子发布第一份单板 A 快照

**Status:** ready-for-agent

- [ ] Prepared Execution Manifest 是实际点数、实际频率轴和有界 required observation map 的唯一执行权威，Mock 输出必须从该 Manifest 派生。
- [ ] 每个正式数据块的身份足以区分 Manifest、prepared execution、Board run、generation、source state、receiver path、wave、sequence 和点范围。
- [ ] Mock 场景先为本轮确定一个 300～400ms 的 run_duration；各数据块的确定性 offset 均落在 Run Accepted 后的 0～run_duration 之间且早于 terminal，唯一 Run Terminal 在 run_duration 到达。场景可以指定乱序，测试不依赖真实随机数或墙钟时间。
- [ ] 完整且互不重叠的数据块即使乱序到达也能成功组装，回调顺序不得成为正式数据位置。
- [ ] Builder 由 Manifest 的观测集合和覆盖范围驱动，不硬编码固定 a/b 数组位置或单板型号分支。
- [ ] A 保存实际轴、Typed Quality Plane 以及单元素 BoardRunEvidence；evidence 包含 Manifest、session/capability、BoardRunId、generation、coverage、sequence 和唯一 terminal 账本。
- [ ] 同一个公共入口连续成功提交两次扫描时产生不同 Snapshot identity，第二次提交不修改第一份 A 的数值、质量或来源信息。
- [ ] 端到端测试只通过 Mock 控制面选择场景和推进虚拟时间，不直接调用 Prepare/Run，也不检查 Builder 私有数组或队列。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
