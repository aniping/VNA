# Vector Network Analyzer

面向公司定制 AArch64 GNU/Linux 5.10 PREEMPT 平台的 VNA 上层软件项目。生产程序使用公司 AArch64 Linux SDK 构建；Windows MinGW 用于 Mock 驱动、开发调试和自动化测试。项目不实现单板底软，而通过统一 Board Adapter 接入真实单板、Mock 和回放数据。

## 当前状态

候选分层架构 v0.1、跨层 Interface、Board Adapter、端到端数据流/生命周期契约与 176 项商用功能矩阵已经完成文档基线，其中商用功能已完成官方证据归类。C++17/MinGW A-only 成功/失败纵切已经闭合：公共 `InstrumentKernel` 接收类型化 raw/diagnostic 请求，依次经过固定容量 Runtime、L4 Acquisition、Mock Board Prepare/Run 和 Store 原子终态提交；成功路径发布按 Operation 查询的不可变 A 层 `CompletedSweepBundle` 历史。当前代码用于关闭关键数据与所有权契约，还不是整机产品规格或完整业务实现。

## 当前可执行纵切

已实现：

- `BoardProvider → OpenedBoard.Execution` 的 discover/open/cached capability、prepare 与 run；
- `MockBoardControl` 虚拟时间，以及确定性的 receiver-wave 多块交付计划、实际点范围、类型化质量标志和 Prepare/Run/非法 Manifest 故障剧本；
- Mock Run 从 `RunAccepted` 起采用 300～400ms 的剧本时长；成功块按确定性 offset 在窗口内分批交付，成功或失败的唯一 terminal 位于窗口末端，标称测试值为 350ms，该时间不包含提交或 Prepare；
- prepare/run 同步 Rejected 时归还全部 move-only 输入且零 callback；
- Accepted 后非内联回调、唯一 terminal 和 terminal 后零回调；
- Runtime 在 `reserve_work` 时同时冻结有限 deadline/budget，并绑定 `receiver + WorkId + generation + completion mailbox`；不同控制消费者不能误取彼此终态，Accepted 后的 dispatch 不再申请完成容量；
- 工作按 `dispatch → start/resume → typed completion` 分步推进，终态先写入预留 mailbox，再由后续 Control pump 可靠交付，提交不等待工作完成；
- 工作返回 `Running` 或尚有 completion 待交付时继续占用 slot；返回 `Draining` 后保持容量占用，直到同一注册收到对应 Drain 终态；
- Store 在 Operation 可见前预留 terminal capacity，初始 commit 失败不派发，已安装 reservation 可在容量压力下提交终态；即使 Accepted 后发生内部 Runtime dispatch 契约故障，也会用该预留容量把同一 Operation、status、fence 和 Event 原子提交为 Failed；
- L2 先提交 Accepted Operation、再 dispatch，Runtime completion 再提交 L5 权威终态的跨层合同测试。
- 公共 A-only 提交只返回 `OperationId`，不接受 `RuntimeWork`、Board token 或输出数组；无 diagnostic 授权、非零 `expected_capability_revision` 与当前 cut 冲突、资源不足或初始 commit 失败均同步拒绝，不创建幽灵 Operation/Event，也不调用 Board；同步错误同时返回稳定的 Admission 阶段、重试、安全影响和已读取的 capability session/revision 事实，不要求调用者解析诊断字符串；
- A-only 上层资源以七个具名 move-only RAII owner 在首次 dispatch 前全量准入；Mock Adapter 还会真实预占同一次 Prepare/Run 的 call、队列和 callback sink execution 槽，并强制其按 `Reserved → Preparing → Prepared → Running → Terminal` 一次性消费；非法 call/sink、跨阶段复用或第二项并发提交均同步零 callback/零幽灵拒绝；实际 Manifest 只能一次性消费精确收窄 capability，按每项观测的真实 64 点分块数核对 Ingress 上界，并以 `source state + receiver path + wave` 完整身份判重，不能扩大频率/点数 envelope；
- Adapter 返回错误的 `PrepareAccepted`/`RunAccepted` 身份时，Acquisition 会提交类型化失败并进入对应 terminal 的 Drain；只有携带清理证据的 `PrepareFailed` 或匹配的 Run terminal 可以解除 owner，`PrepareSucceeded`/`PrepareDraining` 因仍代表底板资源而转入 `Quarantined`；
- `PrepareDraining` 会把具名 Board drain owner 与本地/Board 容量一起转入 `Quarantined`；当前 seam 没有后续排空证明，因此 Kernel 持续隔离整组 owner，绝不谎报 `Drained` 或复用容量；
- Mock 同步 Prepare/Run 拒绝不产生 callback；Prepare 接受后的异步失败必须等携带 cleanup evidence 的唯一 terminal，才允许上层提交失败并释放 owner。Prepare 已成功但 Manifest 无效、Run 同步拒绝，或 Runtime stop/deadline/budget 已阻止进入 Run 时，上层会显式提交 Prepared discard，并在非内联唯一 discard terminal 前持续持有 Prepared 及相关 owner；discard 被拒绝或清理失败则隔离资源。失败路径在同一 Store revision 原子写入 Failed Operation、status、fence 和类型化 Event，保留 phase、Manifest/session/revision、相关 ID、retry class 和执行生命周期 safety impact，且 A/B/Stage/C 正式发布计数保持为 0；
- Prepared Manifest 以有界 required observation map 声明本轮必需接收机波量；Mock 成功输出的点数、身份与形状只从该实际清单派生，不再使用独立场景点数；
- Kernel 用 `2 × ceil(point_count / 64)` 在 O(1) 时间内计算 a/b 的保守正式块数；构造时冻结的 Ingress 或 Buffer Profile 容量不足会在任何 Board execution reservation、Operation 或 Event 之前同步拒绝，默认产品切片容量为 8 块且 Run 开始后不扩张；
- Kernel 在首次派发前从固定 `AcquisitionBufferPool` 原子预留 Profile 声明的回退槽并绑定到 `RunDeliveryGrant`；Mock 用不可转移源数组模拟底软，每个块在 callback 前只复制一次到 Pool，之后 `AcquisitionChunkLease` 只移动槽位指针和 generation；driver-buffer-reuse 剧本会在每次 `on_chunk()` 返回后立刻用毒值覆写源数组，201 点验收仍逐点保留 a/b 复数值和质量；
- 正式 chunk 通过固定容量 `AcquisitionIngress` 把 move-only `AcquisitionChunkLease` 从 Board callback 转交给唯一长期 owner `NetworkObservationBuilder`；拒绝也会消费 payload，回调不生成轴、不写 Store。底软意外多送块时，Ingress 饱和会形成 `IngressRejected/AbortRunCapacityBreach` 账本；Buffer 回退槽耗尽会形成失败 Run terminal，二者均不发布部分 A，并在终态后恰好一次归还资源；
- Builder 按 Manifest 中 `source state + receiver path + wave` 的观测集合和点覆盖重组乱序块，不依赖 callback 顺序或固定 a/b 数组位置；只有实际轴、全部必需覆盖、质量和唯一 Completed terminal 均闭合后才密封 `CandidateCommitLease`，缺少必需观测时不发布部分 A；
- Builder 对整项缺失、内部 gap、冲突重复、部分 overlap、越界范围、成功 terminal 早于完整覆盖、failed terminal 和 Ingress 拒绝形成稳定失败账本；失败 Event 保存 Manifest/Prepared/Run/generation、相关观测、被拒绝块，以及期望点、已接收唯一点和首段缺口摘要。Completed terminal 不能覆盖账本错误，旧 A 不会被补零、最后写入覆盖或后续失败扫描修改；
- L2 在 Runtime Completed 后才把 candidate 交给 Store；Store 在一个 revision 内共同发布不可变 `CompletedSweepBundle`、Completed Operation、status/fence 和完成 Event，receipt 返回后才终结 A-only completion 与 disabled Preview owner；
- Store 成功发布可在有 schema 的 validation 或本地 write staging 阶段被确定性拒绝；两类普通拒绝都在同一个 L2 completion 回合使用 Accepted 时安装的终态预留提交 state-only Failed，A、Completed Event/status/fence 全部不逃逸，candidate 随后唯一 abort，completion/disabled Preview owner 只在失败 commit receipt 后终结；
- 若连预留的 state-only Failed 都因显式 Store integrity fault 无法提交，Kernel 进入可查询的 `StoreFailStop`，保留 candidate、Board/采集 owner 并拒绝新扫描；当前未实现自动或人工恢复命令，不能用析构或重新提交绕过隔离；
- 公开 A 查询返回值副本，可读取实际轴、原始复数值、逐点质量、LogicalSweepId 及单元素 BoardRunEvidence；证据冻结 Manifest、Run/generation、按 callback 顺序记录的 chunk identity/coverage/sequence 和唯一 terminal，不暴露 Store 内部裸 Buffer。连续成功扫描使用不同 Snapshot/LogicalSweep ID，后续提交不修改先前历史；A-only 成功仍不发布 B、Stage、C 或空后继 handoff。

当前明确未实现：

- Board Safety/Maintenance、运行中 abort、完整排空恢复/Replay 和真实底软 Adapter；当前仅闭合 Prepared 未进入 Run 时的显式 discard；
- B/C 处理链、校准、Trace/Marker/Limit、Diagram、文件与诊断；
- Web、SCPI、cpp-httplib、Eigen3 和 JSON；
- 公司 AArch64 SDK 编译与目标机/HIL 验证。

当前单个 Pool 槽最多携带 64 个复数样本，Mock 波形源和正式 A 快照上限为 201 点；一项观测最多 4 块，当前 a/b A-only 的 Operation、必需观测、点、chunk、Event、Ingress 和 Buffer 均有公开编译期上限。driver-buffer-reuse、可预测容量不足、运行期 Ingress 超限与 Buffer 回退耗尽矩阵已由 Mock 合同测试验收；这些结果仍只证明当前项目 Buffer/所有权边界，不能替代公司底软 ABI、真实板卡能力或目标机容量证据。

当前可执行产品组合只包含 `VnaBoardMock`，没有 Real Board Adapter、SafetyLane、RF-off/readback 或 HIL 证据。A-only 授权明确命名为 Mock diagnostics；不得把该纵切连接真实 RF 或宣称具备生产安全能力。
失败事实中的 safety impact 只区分“Run 未接受”“匹配 Run terminal 已观察到”与“资源必须隔离”，不等价于物理 RF-off 或真实单板安全证明。
Mock 可确定性注入错误 Manifest/Prepared/Run/generation、重复 terminal、terminal 后回调、底软源 Buffer 立即复用以及正式接收容量突破。Acquisition 将身份/终态首个违约保存为类型化失败事实，并在 callback 返回后的 Runtime 步骤隔离当前 Board session；同一会话的新执行在 Run 前拒绝，关闭并重新打开得到新 SessionId 后才恢复。Ingress/Buffer 容量突破则闭合当前失败 Run 并释放或转交 owner，不把普通容量事实误报成会话身份隔离。

## MinGW 构建与测试

要求 MinGW-w64 `g++` 与 Ninja 已加入 `PATH`。Windows 配置会拒绝非 MinGW 编译器。

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

测试全程使用虚拟时间，不依赖 wall-clock sleep。`BUILD_TESTING=ON` 时，CMake 从仓库内 `vna/3rdparty/packages/googletest-v1.17.0.zip` 解压固定版本 GoogleTest，并通过 `gtest_discover_tests()` 注册独立测试用例，不需要构建机访问外网；Runtime dispatch 与 Store validation/write/integrity 故障注入也只在该组合中条件编译。生产/RTOS 配置设置 `BUILD_TESTING=OFF` 后不会解压、构建或链接 GoogleTest，也不包含这些测试字段、friend、宏或分支。

`AOnlySubmissionPerformance.RecordsMinimumAndProductMaximumBaseline` 固定执行 8 次 warm-up 和 64 次记录，分别报告 1 点与当前 Mock 产品上限 201 点请求从 submit 到 Accepted 的 median/p95。该结果只用于同机构建比较，不设绝对时延门禁；门禁是提交期间无 Board 等待、无逐点频率轴/数据初始化和大块复制。

## 源码组织

工程采用 Piccolo 风格的运行时主干：根 CMake 只组合 `vna`，`vna/CMakeLists.txt` 再组合三方库、Runtime、Board Adapter 和测试。当前实现位于：

- `vna/source/runtime/core`：基础类型；
- `vna/source/runtime/platform`：平台与 Board seam；
- `vna/source/runtime/resource`：权威事实存储；
- `vna/source/runtime/function`：Operation 与 Instrument 工作流；
- `vna/source/runtime/function/acquisition`：A-only 资源准入与分步 AcquisitionEngine；
- `vna/source/adapter`：Mock 及未来真实单板 Adapter；
- `vna/source/test`：GoogleTest contract/integration 测试。

项目自有头文件统一使用 `.h`，实现文件使用 `.cpp`；文件加入现有 Runtime target 时必须在对应 `CMakeLists.txt` 中显式列出，不使用递归 glob 自动收集源码。

## 设计文档

- [分层架构与跨层流动（首读）](docs/design/layered-architecture.md)
- [跨层 Interface 契约基线](docs/design/interface-contracts.md)
- [Board Adapter Interface 与合同测试契约](docs/design/board-adapter-contract.md)
- [整体系统架构](docs/design/system-architecture.md)
- [端到端数据流与生命周期契约](docs/design/data-flow.md)
- [商用功能逐项对齐矩阵](docs/design/feature-alignment-matrix.md)
- [商用 VNA 功能能力目录](docs/research/commercial-vna-capability-catalog.md)
- [商用 VNA 外部行为基线](docs/research/commercial-vna-behavior-baseline.md)
- [商用 VNA 对象、分析与控制行为的一手证据](docs/research/official-vna-object-and-analysis-evidence.md)
- [商用 VNA Sweep 与采集数据链一手证据](docs/research/official-vna-sweep-acquisition-evidence.md)
- [商用 VNA 校准与处理链一手证据](docs/research/official-vna-calibration-processing-evidence.md)
- [商用 VNA 控制、状态、文件、安全与平台一手证据](docs/research/official-vna-control-state-platform-evidence.md)
- [统一业务语言](CONTEXT.md)
- [架构决策记录](docs/adr/)

架构覆盖完整逻辑扫描、校准/修正/独立验证、Trace/Marker/Limit、Diagram、Math/Memory/Hold/Statistics、参考面与夹具、时域与门控、Web/SCPI 同源控制、状态保存、诊断以及多单板能力适配。

## 实现约束

- 核心语言：C++17。
- Windows 开发工具链：MinGW-w64。
- 生产工具链：公司 AArch64 Linux SDK/交叉编译器。
- 用户允许进入准入评估的候选第三方库：Eigen3、cpp-httplib；“允许候选”不等于工具链已支持或可直接用于生产。
- cpp-httplib 上游当前明确不支持也未测试 MSYS2/MinGW；必须 pin 精确版本，在项目指定的精确 MinGW-w64 上先通过 HTTP core 的编译、链接、运行，再验证 SSE/WebSocket，并由公司 AArch64 SDK/目标机独立复验。基础 HTTP 失败时替换整个 Web HTTP Transport Adapter，不能只切换实时传输方式。
- 任何新增目标端 C/C++ 依赖必须分别通过开发工具链与公司 AArch64 SDK 的编译、链接和目标机冒烟验证。
- Preview 仅用于 Web 实时观察；正式计算、Marker、Limit、保存和 SCPI 查询只读取完整、不可变、原子发布的结果快照。
