# 矢量网络分析

本上下文描述 VNA 上层软件中与激励、接收、测量、校准和结果呈现有关的统一业务语言。

## 采集

**接收机波量（Receiver Wave Quantity）**：
在一个实际激励频点和一条接收路径上，由底软完成解调后产生的、未经用户校准的复数入射波 `a` 或响应波 `b`。它不是 ADC 或 IQ 时域采样序列。
_避免使用_：a/b 原始波形、ADC 波形

**扫频预览（Sweep Preview）**：
当前扫频已经采集但尚未形成完整点集的临时数据。它只用于实时观察，不是可供正式计算或查询的测量结果。
_避免使用_：实时结果、当前结果

**完整扫频快照（Completed Sweep Snapshot）**：
在同一采集配置下成功完成一次逻辑扫频后得到的完整接收机观测点集，对应实现类型 `CompletedSweepBundle`。它是测量处理与校准标准件采集的正式 A 层输入，但不等于某条 Trace 已完成 Marker/Limit 求值。
_避免使用_：当前数组、活动数据

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
由一个或多个完整逻辑扫频经过接收机量提取、兼容平均和用户校准修正后原子发布的不可变 B 层网络结果，对应 `CompletedMeasurementBundle`。它绑定实际激励轴、有界 `AverageContributionRef`、平均 generation/count、配置版本、单板身份和质量信息；项目原生 Sweep completion fence 到这一层为止，不等待所有 Trace 分析。

**测量阶段快照（Measurement Stage Snapshot）**：
当 receiver、ratio、corrected network、fixture/de-embedding/mixed-mode 等非 C 层数据尚未物化时，由 `MaterializeMeasurementStageOperation` 从明确的 A/B 父快照惰性生成的不可变结果。它保存 requested stage、父引用、RF/network graph、Profile、axis、port topology、Z0、unit 和 quality，但不包含 Analysis Trace、Marker 或 Limit revision；Touchstone、全矩阵导出和非 formatted SCPI query 可直接绑定它。

**校准会话（Calibration Session）**：
组织标准件采集和求解过程的有终态操作。成功完成时发布不可变 Correction Set；它本身不表示某个 Channel 正在应用校准。

**修正集（Correction Set）**：
由一次成功校准求解产生的不可变误差模型版本，包含误差项、适用范围、求解器版本和采集来源。

**校准绑定（Correction Binding）**：
Channel 对某个 Correction Set 的版本化选择与独立 correction enable 状态，表示为 `Unbound` 或 `Bound{set_id, set_revision, enabled, policy_revision}`。`Bound(enabled=false)` 关闭修正但保留所选 Set，重新开启时不会猜测目标；一个 Correction Set 可以同时被多个 Channel 使用。

**校准匹配报告（Correction Match Report）**：
把 Correction Set 与本次 Prepared Execution Manifest 比较后的结构化结论，分别表达频率轴、路径、条件、时效、绑定和总体适用性，不压缩成单一“有效/无效”布尔值。

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
一次 Trace 求值冻结的有类型来源：Live 输入引用一个 B 层 `measurement_snapshot_id`；Frozen/Memory 输入引用静态快照；Imported 输入引用导入数据快照；Derived 输入引用一个或多个上游 C 层 publication，并记录同步政策和 generation vector。非 Live Trace 不伪造 Sweep 或 B 层父对象。

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

## 控制与执行

**操作（Operation）**：
扫频、平均序列、测量阶段物化、校准/校准验证、迹线重算、保存恢复、导出和诊断等异步工作的统一可等待生命周期。原生 `SweepOperation` 在 B 层 Measurement 发布后完成；`MaterializeMeasurementStageOperation`、`EvaluateTraceOperation`、`AverageSequenceOperation` 有各自终态。Web 事件和 SCPI `*OPC?` 等同步机制按 Compatibility Profile 等待明确 Operation/fence，而不是读取全局忙碌布尔值。

**执行上下文（Execution Context）**：
所有 Measurement Pipeline 求值、Calibration solve/verification、Persistence 和 Diagnostics 长操作入口必须显式接收 `ExecutionContext{stop_token, monotonic_deadline, BudgetHandle, ProgressSink}`。可协作计算在有界间隔检查取消和 deadline；不可中断的第三方/OS 调用只能进入隔离 worker/lane，超时后由可见 Drain Operation 继续持有容量、输入 lease 和临时资源，不能因父 Operation 先返回终态就提前复用。

**恢复激活策略（Recall Activation Policy）**：
State Recall 的 RF/运行态边界。默认 `RestoreInHoldSafeOff`：只原子恢复已验证配置和静态结果，所有 Channel 保持 Hold、RF safe/off，不恢复 Continuous/Groups/Armed/WaitingTrigger 或未完成 Operation。`ExplicitRestoreRunState` 需要额外授权，仍先恢复到安全 Hold，再以新的完整 admission Operation 启动；异常重启永远使用安全默认，普通 State 文件不能携带可绕过授权的 auto-run 能力。

**客户端会话（Client Session）**：
每个连接的身份、权限、输入输出、游标和 Transport/parser 生命周期。SCPI 的 Error FIFO、ESR/ESE/SRE 和 read-clear 在语义上属于该 Session，但权威可变值保存在 Control Executor 所有的 `ScpiSessionStateCatalog`，ClientSession 只持 session ID；共享 Operation/Questionable 状态保存在 Instrument `StatusRegisterCatalog`。Web 的页面选择始终是 session-local；SCPI 的 Active/Selected Channel、Trace、Marker 或 Diagram 作用域由 Compatibility Profile 决定，可以是共享仪器状态、每 Channel 共享状态或连接局部状态。Query 接受时先解析并固定目标，再固定正式快照。
