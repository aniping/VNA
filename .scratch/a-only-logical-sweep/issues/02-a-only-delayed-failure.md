# 02 — 让 A-only 命令走到 Mock 延迟失败终态

**What to build:** 开发者从公共仪器入口提交一次明确授权的单板 A-only 扫描；接受分支只返回任务编号，拒绝分支返回类型化原因。软件在开始单板工作前完成全部必要检查和容量预留，然后让 Mock 单板经历 Prepare、Run 和延迟失败，最终形成一个一致的失败任务且不产生正式扫描数据。

**Blocked by:** 01 — 让扫描任务真正等待异步单板

**Status:** ready-for-agent

- [x] 公共提交入口接收类型化 A-only 扫描请求并返回明确的 SubmitResult：Accepted 分支携带 OperationId，Rejected 分支携带类型化错误；调用者不能传入内部 RuntimeWork、单板令牌或输出数组。
- [x] A-only 必须携带明确的 raw/diagnostic 授权；普通 Channel Sweep 不能借该入口跳过后续测量处理。
- [x] 提交热路径只处理紧凑元数据和资源凭证，不生成实际频率轴、不初始化点数据，也不执行随扫频点数增长的大块复制。
- [x] Runtime、Store 生命周期终态、A 输出/candidate、采集 Buffer/Ingress、Board call/sink、A-only completion owner、disabled Preview owner 和精确收窄能力都在首次派发前取得。
- [x] Accepted 与终态预留先原子可见，再执行非内联派发；任一初始准入或提交失败均不创建 Operation、Event 或 Board 调用。
- [x] Prepare 返回的实际清单只能在已准入的保守范围内本地收窄；不得在采集层反向申请新容量、扩大计划或切换单板。
- [x] Mock Run 的失败终态由场景在 300～400ms 虚拟时间内确定性调度，标称值为 350ms；该时间仅表示 Run Accepted 到 Run Terminal。
- [x] 在预定 Run 终态之前，提交调用已经返回，Operation 保持 Accepted/Running 且没有 A；到达预定时刻后，Operation、status/fence 和失败 Event 在同一 Store revision 变为失败事实。
- [x] 失败路径不产生 A、B、Stage、C、successor dispatch 或空 continuation handoff，全部 move-only owner 恰好释放或转交一次。
- [x] 记录非门禁的 MinGW“提交到 Accepted”性能基线：在同一构建和机器上固定 warm-up 与重复次数，分别测量最小点数和产品上限点数请求，报告 median 与 p95；验收门禁是无 Board 等待、无逐点轴生成/初始化/大块复制，而不是某个开发机绝对毫秒值，更不能拿 350ms 当提交阈值或 RTOS WCET。
- [x] 产品组合只能使用 Mock 路径，不能启用 Real Board 或宣称具备生产 RF 安全能力。
- [x] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。

## Comments

- 2026-07-19：按公共 L2 `submit_a_only()`、确定性 `run_one()` 和 Store 只读事实面进行 TDD。红灯依次证明缺少类型化入口、350ms 延迟失败/原子事实面、Manifest one-shot 收窄、显式关键资源集合，以及 continuation expiry 错误使用绝对 Board tick；均已转绿。
- 2026-07-19：候选实现完整测试通过 30/30；`BUILD_TESTING=OFF` 的 Runtime 与 Mock 产品目标构建通过，`build.ninja` 无 GTest/googletest/test source 引用且未生成 `_deps/googletest-src`。
- 2026-07-19：同一 MinGW Debug 构建与机器的非门禁基线使用 8 次 warm-up、64 次记录：1 点 median 900ns、p95 1000ns；201 点 median 900ns、p95 1000ns。350ms 仅为 Mock Run Accepted→Terminal，不是提交阈值或 RTOS WCET。候选等待规范/需求双审核。
- 2026-07-19：首轮双审核发现三项阻断：Board call/sink 只有位掩码声明而未真实预占、异常 Accepted 身份会让非 owning sink 提前失活、关联提交未拒绝零 WorkId/plan digest。新增并发零幽灵、恶意 Adapter Drain 保活和 Store 关联校验红测试后，分别用 Adapter `BoardExecutionReservation`、七项具名 RAII owner、按 Prepare/Run 区分的 Drain obligation 及兼容旧入口的强关联校验修复；等待修复后完整验证与第二轮双审核。
- 2026-07-19：审核修复后完整测试通过 34/34，`BUILD_TESTING=OFF` 产品目标通过且构建图无 GTest/googletest/test source。相同 8 次 warm-up、64 次记录的最新非门禁基线：1 点 median 1300ns、p95 1400ns；201 点 median 1300ns、p95 1400ns；提交仍无 Prepare/Run 调用、逐点轴生成或大块复制。等待第二轮双审核。
- 2026-07-19：第二轮双审核继续发现 execution reservation 可 move 赋值遗失、同一租约可重复 Prepare/Run、零 call/无效 sink 未拒绝，以及 `PrepareDraining` 被误报 `Drained`。红测试闭合后，reservation 改为仅 move 构造并使用一次性 phase/identity 状态机；具名 `BoardPrepareDrainOwner` 与全部容量在 `Quarantined` 后继续隔离。机械重复的七个构造器同时收敛为私有 tagged owner 模板，保留七个不同静态类型。完整测试通过 37/37；关闭测试产品构建及无 GTest 检查通过；性能复测仍为 1/201 点 median 1300ns、p95 1400ns。等待第三轮双审核。
- 2026-07-19：第三轮双审核发现两个阻断：错误 `PrepareAccepted` 身份后的 Drain 把 `PrepareSucceeded`/`PrepareDraining` 误当作清理终态，以及 Mock Run 未绑定同一 reservation 实际签发的 Prepared 身份。新增三种 Prepare terminal 资源语义与伪造 Prepared identity 红测试后，Drain 仅允许 `PrepareFailed(cleanup)` 报 `Drained`，其余两类保留全部 owner 并报 `Quarantined`；Mock 保存并精确校验本 reservation 的 `prepared_id + manifest_digest`。完整测试通过 40/40；关闭测试产品构建通过且无 GTest 引用；8 次 warm-up、64 次记录的最新非门禁基线为 1 点 median 1300ns、p95 1400ns，201 点 median 1350ns、p95 1400ns。最终需求复审 0 项 blocking/0 项 judgement，最终规范复审 0 项 blocking/1 项非阻塞 judgement，双审核通过；非阻塞建议是后续按测试族拆分已超过千行的跨层契约测试文件。
