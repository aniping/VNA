# 02 — 让 A-only 命令走到 Mock 延迟失败终态

**What to build:** 开发者从公共仪器入口提交一次明确授权的单板 A-only 扫描；接受分支只返回任务编号，拒绝分支返回类型化原因。软件在开始单板工作前完成全部必要检查和容量预留，然后让 Mock 单板经历 Prepare、Run 和延迟失败，最终形成一个一致的失败任务且不产生正式扫描数据。

**Blocked by:** 01 — 让扫描任务真正等待异步单板

**Status:** ready-for-agent

- [ ] 公共提交入口接收类型化 A-only 扫描请求并返回明确的 SubmitResult：Accepted 分支携带 OperationId，Rejected 分支携带类型化错误；调用者不能传入内部 RuntimeWork、单板令牌或输出数组。
- [ ] A-only 必须携带明确的 raw/diagnostic 授权；普通 Channel Sweep 不能借该入口跳过后续测量处理。
- [ ] 提交热路径只处理紧凑元数据和资源凭证，不生成实际频率轴、不初始化点数据，也不执行随扫频点数增长的大块复制。
- [ ] Runtime、Store 生命周期终态、A 输出/candidate、采集 Buffer/Ingress、Board call/sink、A-only completion owner、disabled Preview owner 和精确收窄能力都在首次派发前取得。
- [ ] Accepted 与终态预留先原子可见，再执行非内联派发；任一初始准入或提交失败均不创建 Operation、Event 或 Board 调用。
- [ ] Prepare 返回的实际清单只能在已准入的保守范围内本地收窄；不得在采集层反向申请新容量、扩大计划或切换单板。
- [ ] Mock Run 的失败终态由场景在 300～400ms 虚拟时间内确定性调度，标称值为 350ms；该时间仅表示 Run Accepted 到 Run Terminal。
- [ ] 在预定 Run 终态之前，提交调用已经返回，Operation 保持 Accepted/Running 且没有 A；到达预定时刻后，Operation、status/fence 和失败 Event 在同一 Store revision 变为失败事实。
- [ ] 失败路径不产生 A、B、Stage、C、successor dispatch 或空 continuation handoff，全部 move-only owner 恰好释放或转交一次。
- [ ] 记录非门禁的 MinGW“提交到 Accepted”性能基线：在同一构建和机器上固定 warm-up 与重复次数，分别测量最小点数和产品上限点数请求，报告 median 与 p95；验收门禁是无 Board 等待、无逐点轴生成/初始化/大块复制，而不是某个开发机绝对毫秒值，更不能拿 350ms 当提交阈值或 RTOS WCET。
- [ ] 产品组合只能使用 Mock 路径，不能启用 Real Board 或宣称具备生产 RF 安全能力。
- [ ] 新增或修改的公开接口具备完整 Doxygen 契约；相关测试、完整测试集及关闭测试的产品构建均通过。
