# 矢量网络分析

本上下文描述 VNA 上层软件中与激励、接收、测量、校准和结果呈现有关的统一业务语言。

## 架构层与数据阶段

项目统一使用 [六层职责模型](docs/design/layered-architecture.md)：L1 协议 Adapter、L2 仪器应用、L3 Operation Runtime、L4 领域执行、L5 权威事实、L6 资源 Adapter 与平台。层表示职责和依赖方向，不要求每个调用机械穿过全部层；`Instrument Kernel` 是控制主干，L3 调度 L4，只有 L2 可以通过 `DomainCommitBundle` 让 L5 的事实原子可见。

A/B/Stage/C 是 L5 中的正式**数据阶段**，不是四个软件层。Marker/Limit 定义在 L2、求值在 L4、结果随 C 存入 L5、最后由浏览器呈现；Diagram 只组织 C 的显示引用，不参与测量判定。`BoardPort` 是 L4 Acquisition 拥有的硬件 seam，Real/Mock/Replay 是它的 L6 Adapter；每块板向上暴露一个 `BoardSession` Interface，不能把单板差异带入 Channel、Trace、Calibration 或 Diagram 语义。

## 采集

**接收机波量（Receiver Wave Quantity）**：
在一个实际激励频点和一条接收路径上，由底软完成解调后产生的、未经用户校准的复数入射波 `a` 或响应波 `b`。它不是 ADC 或 IQ 时域采样序列。
_避免使用_：a/b 原始波形、ADC 波形

**扫频预览（Sweep Preview）**：
当前扫频已经采集但尚未形成完整点集的临时数据。它只用于实时观察，不是可供正式计算或查询的测量结果。
_避免使用_：实时结果、当前结果

**完整扫频快照（Completed Sweep Snapshot）**：
在同一采集配置下成功完成一次逻辑扫频后得到的完整接收机观测点集，对应实现类型 `CompletedSweepBundle`。它绑定 `LogicalSweepId + BoardRunEvidence[]`；每项 evidence 保存一块板的 Manifest、BoardRun generation 与完成账本，默认单板时数组长度为 1。它是测量处理与校准标准件采集的正式 A 层输入，但不等于某条 Trace 已完成 Marker/Limit 求值。
_避免使用_：当前数组、活动数据

**采集块租约（Acquisition Chunk Lease）**：
Board Adapter 将一块接收机观测唯一移动给 Acquisition Ingress 的所有权凭证。若公司底软允许转移 buffer 生命周期，它包装该 buffer；若底软在回调返回后立即复用内存，Adapter 必须先复制到项目 Buffer Pool。正式 Builder 是唯一长期拥有者，Preview 只能读取有界只读视图或独立 `PreviewTile`，不能与 Builder 共同拥有同一个 move-only chunk。

**类型化质量平面（Typed Quality Plane）**：
与数值同路传播的 validity、quality flags、有界指标及其实际 granularity。不同层可以是 Sweep、receiver、path、point 或 matrix-element 粒度；ratio、平均、校准、矩阵变换和 Trace 节点按版本化 quality transform 产生下一层质量，不能用日志附言或一个全局 `valid` 布尔值代替。

**准备执行清单（Prepared Execution Manifest）**：
Sweep Compiler 先从请求生成 `SweepIntent` 与保守资源声明；Resource Arbiter 依据当前 ResourceGraph topology epoch 做保守预准入，再允许 Board Adapter `prepare` 形成不可变实际执行事实。Manifest 包含量化轴、功率、IFBW、路由、source state、资源和内存界限；校准匹配、精确资源/处理容量预留和预准入 token 到 `ExecutionLease` 的升级只能使用它，升级成功后才可 `start`，不能用未量化请求值替代。

**逻辑扫频（Logical Sweep）**：
为产生一个完整测量结果而必须原子完成的全部采集动作。完整双端口 S 参数测量通常包含正向、反向及板卡误差模型要求的辅助观测；任何必要动作失败都不发布部分网络结果。

**平均域（Averaging Domain）**：
明确一次复数平均读取哪个数据阶段。`AveragePolicy` 另用 `sample_boundary` 表达 Point/SourceState/LogicalSweep，二者不能混成一个枚举。项目原生推荐 `MeasuredRatio + LogicalSweep`；Keysight ENA 可在平均前包含明确标识的端口/工厂特性修正，但用户校准 Error Terms 在平均后应用。CMT 内部数组型 Profile 可用 `ReceiverWaves + LogicalSweep/Point`（随后形成 ratio），但必须按目标型号/固件回归。不得把显示格式标量平均当作复数平均，也不得隐式叠加底软与上层平均。

**平均模式（Average Mode）**：
`FiniteBatch | SlidingWindow | Cumulative | VendorRunning` 的类型化语义。Finite 有有限 factor；Sliding 使用预留的固定 contribution ring 保存逐点复数 contribution/weight/quality，可在 A 层快照回收后仍精确移出最老样本；Cumulative 保存有界 accumulator；VendorRunning 只有在 Compatibility Profile 同时冻结 update kernel 与 state schema 后才允许。不能用一个含糊的 `running` 布尔值代替这些不同状态。

**平均贡献引用（Average Contribution Ref）**：
B 层快照对平均输入的有界来源证明。Finite Average 可保存受 factor 上限约束的显式 `source_sweep_ids`；Sliding Average 保存有界窗口 ID、固定 contribution ring 的不可变结构共享引用与 accumulator snapshot；Cumulative/VendorRunning Average 保存 `average_accumulator_snapshot_id`、generation/count、首末输入序号范围和滚动强摘要，详细逐 Sweep 审计另受有界保留策略约束。任何持续运行模式都不得让 B 层元数据随时间无界增长。

**单板适配器（Board Adapter）**：
隔离公司底软或 Mock 实现的唯一硬件 seam。它负责报告能力、准备和执行逻辑扫描、上送运行 phase/接收机观测、提供跨线程 abort 与 RF safe-state 过渡及健康信息，但不实现用户校准、Marker、Limit、Diagram 或协议业务。

**单板能力描述（Board Capabilities）**：
单板可执行频率、功率、IFBW、点数、端口路由、触发、接收机拓扑、波量定义、质量标志、并发资源、abort SLA、RF safe/off，以及 Clock/Coherence Domain、timebase lock、同步 trigger/epoch 与最大 skew 的版本化事实。上层根据它验证和编译扫描，不根据板卡型号散布条件分支；未知相干能力不得把多块板的数据合成同一代 S-matrix、mixed-mode 或校准 bundle。

**单板安全通道（Board Safety Lane）**：
每块可发射 RF 的单板必须预留、且不与 Acquisition/Prepare/Recovery worker 共用的 RF-off/readback 通道。safe-state 请求通过唯一 `BoardSafetyCallId` 报告 accepted/rejected 和一个可信终态；若该通道卡死则进入 Drain/Quarantine，仍应存在与其物理独立的 emergency kill/interlock。软件 quarantine 本身不能证明 RF 已关闭。

**相干域（Coherence Domain）**：
一组被硬件能力明确保证共享/锁定 timebase、同步 trigger/epoch、相位关系与 skew 上界的采集资源。项目默认一个 Logical Sweep/校准采集只绑定一个 Board Session；只有 Product Profile 和所有参与 Board Capabilities 能共同证明同一 Coherence Domain 与实际轴兼容时，才允许跨板组成一个相干网络结果。

**RF 安全故障（FaultUnsafeRf）**：
Abort/timeout 后无法通过独立控制路径和 readback 证明 RF 已关闭的锁存状态。软件 quarantine 只阻止再次调度；FaultUnsafeRf 还必须禁止普通恢复、触发硬件 interlock/kill（若有），并要求授权人员物理隔离/断电和独立安全验证后才能清除。

## 测量与校准

**测量规格（Measurement Spec）**：
描述“测什么”的值对象，例如 `S11`、`S21`、接收机波量或接收机比值。它属于一条 Analysis Trace 的 Source Spec，不单独拥有用户可见身份；不同 Trace 可以包含等价规格，Sweep Compiler 只合并其底层采集需求，不合并 Trace 的身份和设置。

**测量完成快照（Completed Measurement Snapshot）**：
由一个或多个完整逻辑扫频经过接收机量提取、兼容平均和用户校准修正后原子发布的不可变 B 层网络结果，对应 `CompletedMeasurementBundle`。它绑定实际激励轴、有界 `AverageContributionRef`、平均 generation/count、配置版本、逐板 identity/capability 集合和质量信息；默认单板集合长度为 1。项目原生 Sweep completion fence 到这一层为止，不等待所有 Trace 分析。

**测量阶段快照（Measurement Stage Snapshot）**：
当 receiver、ratio、corrected network、fixture/de-embedding/mixed-mode 等非 C 层数据尚未物化时，由 `MaterializeMeasurementStageOperation` 从明确的 canonical A/B roots 惰性生成的不可变结果。它保存 requested stage、完整 RF/network graph revision、Profile、axis、port topology、Z0、unit 和 quality，但不包含 Analysis Trace、Marker 或 Limit revision；一个 Stage 不把另一个 Stage 当作正式父对象，图内中间值只属于私有缓存。Touchstone、全矩阵导出和非 formatted SCPI query 可直接绑定它。

**校准观测快照（Calibration Observation Snapshot）**：
一个 Calibration Step 的某次成功 Acquisition Attempt 交付给 Solver 的正式不可变输入。它绑定 session/step/attempt、标准件与模型 revision、方法、实际轴/路由/端口拓扑、逐板 identity/capability/evidence 集合、独立 Average Policy/generation/count/complete、有界 contribution closure、质量，以及 canonical A/Stage refs。若使用 Stage，其 canonical root 集合必须与本次 Attempt 接受的完整 A 集合**恰好相等**，stage/graph 必须被冻结的 `CalibrationMethodSpec` 明确允许；不得引入用户/当前 `CorrectionSet`、DUT B 或 DUT 分析结果，也不借用当前 DUT Channel 的 last-good B。

**校准会话（Calibration Session）**：
组织标准件采集和求解过程的有终态操作。成功完成时发布不可变 Correction Set；它本身不表示某个 Channel 正在应用校准。

**修正集（Correction Set）**：
由一次成功校准求解产生的不可变误差模型版本，包含误差项、适用范围、求解器版本、逐板 identity/capability/path condition 集合和 Observation/A evidence 来源；默认单板时集合长度为 1。

**校准绑定（Correction Binding）**：
Channel 对某个 Correction Set 的版本化选择与独立 correction enable 状态，表示为 `Unbound` 或 `Bound{set_id, set_revision, enabled, policy_revision}`。`Bound(enabled=false)` 关闭修正但保留所选 Set，重新开启时不会猜测目标；一个 Correction Set 可以同时被多个 Channel 使用。

**校准匹配报告（Correction Match Report）**：
把 Correction Set 与本次非空 `PreparedExecutionManifestSet` 比较后的结构化结论，逐板表达 identity/capability/path/condition 匹配，再聚合频率轴、时效、绑定和总体适用性；默认单板时集合长度为 1，不能只检查其中一块板，也不压缩成单一“有效/无效”布尔值。

**校准验证（Calibration Verification）**：
校准完成后的独立 Pro 工作流。`VerificationPlanRevision` 固定被验证的 Correction Set、独立 verification artifact 的 characterization、端口/实际轴、所需 S 参数、tolerance/uncertainty 和算法版本；`CalibrationVerificationOperation` 消费正式 B 层测量并发布不可变 `CalibrationVerificationResultSnapshot`，给出逐点 residual/margin 与 Pass/Fail/Indeterminate。它不重新求解、不改变 Correction Binding，也不把 system/confidence check 冒充单个仪器或标准件认证。

**类型化处理图（Typed Processing Graph）**：
连接校准后网络、参考面移动、参考阻抗转换、夹具、混模、时域、门控、Trace Math 和格式化等节点的版本化图。每个节点声明输入输出数据阶段、轴、端口拓扑、参考面、Z0、有效性传播和 Preview 能力。

## 分析与显示

本节是项目内部的归一化语言，不宣称 Keysight、R&S、CMT 公开了相同的内部对象切分。协议 Adapter 负责把 Keysight 的 Measurement/显示 Trace、R&S/CMT 的复合 Trace 映射到这些对象。

**分析迹线（Analysis Trace / Trace Definition）**：
面向用户和协议的稳定可分析对象。它拥有 `TraceSourceSpec`、处理图、分析投影、Marker、Limit、Memory、Hold 与统计定义；Live Source 内含 Measurement Spec，其他 Source 可以引用 Math、Frozen/Memory Snapshot 或导入数据。它独立于任何 Diagram，可在不重新扫频的情况下基于 last-good 数据重新求值。

**迹线求值快照（Trace Evaluation Snapshot）**：
某个正式输入快照与某个 Analysis Trace 版本计算得到的不可变结果。Marker、Limit、SCPI 查询和导出必须绑定明确的求值快照。

**分析输入引用集合（Analysis Input Ref Set）**：
一次 Trace 求值冻结的有类型来源：Live 输入引用一个 B 层 `measurement_snapshot_id`，需要矩阵处理分支时也可引用一个 `measurement_stage_snapshot_id`；Frozen/Memory 输入引用静态快照；Imported 输入引用导入数据快照；Derived 输入引用一个或多个上游 C 层 publication，并记录同步政策和 generation vector。非 Live Trace 不伪造 Sweep 或 B 层父对象。

**分析发布（Analysis Publication）**：
把 Analysis Input Ref Set、Analysis Trace revision、Marker/Limit revision 和求值结果绑在一起的 C 层不可变发布。单条 Trace 求值失败只使该发布失败或 stale，不回滚任何已经成功的父快照；Live Trace 仍可沿输入引用反查 B 层 Measurement。失败 Sweep 不产生以该 Sweep 为父的新 A/B，也不产生以其结果为父的新 Live C；基于旧 B 的重算以及 Frozen/Imported/Derived C 与该失败独立。

**标记定义（Marker Definition）**：
属于 Analysis Trace 的分析规则，包括普通、参考、差值、固定、跟踪以及峰值、目标、带宽等搜索。标记计算结果与定义分开，并记录输入节点、数据投影和父快照。厂商兼容层可以用 Measurement 或 Trace 路径寻址它。

**限制测试定义（Limit Test Definition）**：
属于 Analysis Trace 的分段标量判定规则，描述上限、下限、断开段、插值和无效点政策。Limit Test enable 与显示可见性分开；Limit Line 是同一定义的显示 Overlay，不能另用一套判定算法。

**迹线放置（Trace Placement）**：
Diagram 对 Analysis Trace 的显示关联，只包含可见性、颜色、线型、坐标轴分配、缩放/reference、层级及该 Placement 的 Marker/Limit overlay 样式等视图属性。它有与 Analysis Trace 不同的作用域标识，但不是另一个测量对象；Product/Compatibility Profile 可以限制一条 Trace 允许的 Placement 数量。

**图表（Diagram）**：
实际绘图区，拥有坐标系、轴、网格、标题、Trace Placement 顺序和布局，并渲染各 Placement 的 Marker/Limit overlay。它不拥有 Analysis Trace、Marker/Limit 业务定义或正式分析结果。项目核心把删除 Placement、删除 Analysis Trace、删除 Diagram 和删除 Channel 建模为不同 Command；厂商兼容命令的组合副作用由 Compatibility Profile 映射。

**图表帧引用集合（Diagram Frame Ref Set）**：
运行期 Diagram View Catalog 中的一次刷新选择，为每个 Trace Placement 固定确切 `analysis_publication_id`。普通视觉叠加可以选择各 Placement 最新可用结果但必须显示 generation/stale；跨 Trace Math、Marker coupling 或共享 Limit 必须先满足更严格的同步政策再原子切帧。它是结果选择器，不是新的数据处理层，也不会因每轮刷新改写持久 Workspace revision。

## 控制与执行

**操作（Operation）**：
扫频、平均序列、测量阶段物化、校准/校准验证、迹线重算、保存恢复、导出和诊断等异步工作的统一可等待生命周期。原生 `SweepOperation` 在 B 层 Measurement 发布后完成；`MaterializeMeasurementStageOperation`、`EvaluateTraceOperation`、`AverageSequenceOperation` 有各自终态。Web 事件和 SCPI `*OPC?` 等同步机制按 Compatibility Profile 等待明确 Operation/fence，而不是读取全局忙碌布尔值。

**执行上下文（Execution Context）**：
所有 Measurement Pipeline 求值、Calibration solve/verification、Persistence 和 Diagnostics 长操作入口必须显式接收 `ExecutionContext{stop_token, monotonic_deadline, BudgetHandle, ProgressSink}`。可协作计算在有界间隔检查取消和 deadline；不可中断的第三方/OS 调用只能进入隔离 worker/lane，超时后由可见 Drain Operation 继续持有容量、输入 lease 和临时资源，不能因父 Operation 先返回终态就提前复用。

**操作输入租约集合（Operation Input Lease Set / Pinned Input Set）**：
Control Executor 在派发计算前一次性 pin 住全部 typed parent snapshots、共享 Buffer 与所需元数据的 RAII 集合。Worker 或 Drain 在真实 terminal 前拥有它，防止多输入计算读到一半时某个父 payload 被 retention 回收；产生正式 publication 的 Board/Processing/Calibration worker 只能返回 `PublicationCandidateBatch`，不能借此直接发布 Catalog、更新 Head 或发送 Event。Persistence/Diagnostics 则返回各自 deep Module 定义的 Result，由 Control Executor 决定是否转换为领域 candidate。

**发布候选批与候选提交租约（Publication Candidate Batch / Candidate Commit Lease）**：
产生正式 publication 的 Board/Processing/Calibration worker 对冻结输入计算后返回的唯一候选载体，包含待发布的 snapshot graph、输出 reservation 和校验信息。批从 worker return 起到 commit 或 abort 终止始终持有 `CandidateCommitLease`，保证候选 payload、父引用和预算不会落入无人拥有的间隙；它不是已发布事实，任何查询或事件都不得提前观察。

**领域提交批（Domain Commit Bundle）**：
Control Executor 把可选的 `PublicationCandidateBatch`、有类型的 `DomainCatalogPatchSet` 与 Head、Operation/fence、Instrument Status Register、SCPI Session State、WaitRegistry、QueryTicket/ResultPin、EventJournal 和 retention patches 组合成一次提交，由 `DomainCommitCoordinator` 持 `DomainCommitPermit` 全有或全无地生效。Domain Catalog patch 覆盖 Instrument/Channel/Calibration/Analysis/Display 等小型可变 revision，不得退化成任意 key/value。典型提交包括 B + accumulator snapshot + `ChannelAverageHead` + `ChannelMeasurementHead`，CorrectionSet publication + CalibrationSession terminal，或可提升的当前 Live C closure + `TraceAnalysisHead`；只读取既有结果时可没有候选批，但 direct Ready admission 或 Pending→Ready 都必须在同一次提交中取得对应 `ResultPinLease`。失败时不得留下可见 snapshot、半更新领域 revision、已推进 Head、未锁存的 status、丢失的 waiter wakeup、Ready ticket 或孤立 Event。

**结果闭包租约（Result Closure Lease）**：
QueryTicket Ready 时由 `ResultPinLease` 保活的自包含结果闭包，例如 C publication 连同 Trace/Marker/Limit children、axis、quality 及结构共享 Buffer，而不是只 pin 一个顶层 ID。`open_read` 在 Ticket Ready→Reading 时将它原子转换为 `ReaderLease`，传输 terminal 再与 Reading→Consumed/Failed 同批释放；Event 不持有该租约，祖先 payload 可以按 retention 回收，但最小 tombstone/digest/provenance 保留。

**测量与分析头（Measurement / Analysis Head）**：
Catalog 中选择“最近尝试”和“last-good 正式结果”的小型可变记录。`ChannelMeasurementHead` 与 `TraceAnalysisHead` 在失败时更新 attempt/status，但不改写不可变 B/C。`ChannelAverageHead{generation, current_accumulator_snapshot_id, count, complete, revision}` 是下一次平均贡献选择权威 accumulator 的唯一入口；每次接受贡献的 B、accumulator snapshot、`ChannelAverageHead` 和 `ChannelMeasurementHead` 必须在同一个 `DomainCommitBundle` 切换。只有匹配 expected current-input token 的 Live C 可以 compare-and-set 提升 `TraceAnalysisHead`；历史 B/Stage exact query 默认只返回 C，不倒退 Head。stale 是 Head、当前配置和 last-good 之间的关系，不是历史 Snapshot 被原地修改。

**正式数据权威（Formal Data Authority）**：
Measurement Data Store 与 SnapshotCatalog 共同持有 A/B/Stage/C、Calibration Observation、质量平面、父闭包和 retention 事实。Operation 只表示工作，Event 只提示 commit，QueryTicket 只表示调用者的访问能力；任何一者都不能替代正式数据权威。

**恢复激活策略（Recall Activation Policy）**：
State Recall 的 RF/运行态边界。默认 `RestoreInHoldSafeOff`：只原子恢复已验证配置和静态结果，所有 Channel 保持 Hold、RF safe/off，不恢复 Continuous/Groups/Armed/WaitingTrigger 或未完成 Operation。`ExplicitRestoreRunState` 需要额外授权，仍先恢复到安全 Hold，再以新的完整 admission Operation 启动；异常重启永远使用安全默认，普通 State 文件不能携带可绕过授权的 auto-run 能力。

**客户端会话（Client Session）**：
每个连接的身份、权限、输入输出、游标和 Transport/parser 生命周期。SCPI 的 Error FIFO、ESR/ESE/SRE 和 read-clear 在语义上属于该 Session，但权威可变值保存在 Control Executor 所有的 `ScpiSessionStateCatalog`，ClientSession 只持 session ID；共享 Operation/Questionable 状态保存在 Instrument `StatusRegisterCatalog`。Web 的页面选择始终是 session-local；SCPI 的 Active/Selected Channel、Trace、Marker 或 Diagram 作用域由 Compatibility Profile 决定，可以是共享仪器状态、每 Channel 共享状态或连接局部状态。Query 接受时先解析并固定目标，再固定正式快照。
