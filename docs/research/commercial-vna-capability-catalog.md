# 商用 VNA 功能能力全景与首版范围基线

> 调研对象：Keysight PNA/ENA、Rohde & Schwarz ZNA/ZNB、Copper Mountain Technologies（CMT）VNA 的官方帮助、用户手册和编程手册。核对日期：2026-07-17。

## 1. 本文用途与证据边界

本文不是菜单抄录，也不是对某一家仪器的兼容承诺。它用于回答三个问题：

1. 一款不是“玩具”的 VNA 上层软件，功能面至少要完整到什么程度；
2. 哪些能力应进入首版成熟核心，哪些应作为专业扩展，哪些必须由单板能力或商业选件决定；
3. 领域模型、驱动适配层、Web 与 SCPI 接口必须提前为哪些能力留下稳定边界。

公开资料能证明厂商暴露给用户的对象、命令、文件和行为，不能证明闭源产品内部采用了何种线程、消息队列、缓存、类层次或算法实现。下文的分层是本项目根据跨厂商外部行为做出的归一化设计输入，不冒充任何厂商内部架构。校准求解算法、误差模型细节和精度指标也不能仅凭命令手册复制；正式产品必须经过计量验证。

范围标签：

- **首版成熟核心（Core）**：首个可交付版本缺少它就不能称为成熟 VNA 上层软件；必须在真实驱动和 Mock 驱动下具有一致语义。
- **专业扩展（Pro）**：行业常见且架构必须预留，但可以在核心闭环稳定后交付。
- **硬件/选件相关（HW/Option）**：只有单板、外设、端口拓扑或授权能力存在时才能启用；不得用空菜单或假数据冒充支持。

三家资料对完整功能面的共同入口可参见 [Keysight PNA/PNA-X Help](https://helpfiles.keysight.com/csg/N52xxB/Home.htm)、[R&S ZNB/ZNBT User Manual](https://www.rohde-schwarz.com/sg/manual/rs-znb-user-manual_78701-29151.html) 和 [CMT SCPI Command Tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/scpi-command-tree.html)。CMT 的命令树尤其清楚地把校准、触发、显示、标记、限制测试、时域、夹具、文件和能力查询列为独立功能面。

## 2. 顶层能力地图

成熟产品不是 `Channel -> Trace -> Chart` 四个对象的组合，而是下列相互协作的能力域：

```text
Instrument
├─ Identity / Capabilities / Preset / State / Health
├─ Hardware Resources
│  ├─ Sources / Receivers / Physical Ports / Routes
│  └─ Trigger I/O / Reference Clock / Sensors / Options
├─ Channels
│  ├─ Stimulus & Sweep Plan
│  ├─ Acquisition & Trigger Policy
│  ├─ Analysis Traces -> MeasurementSpec / Marker / Limit / Memory
│  ├─ Completed Network & Trace Snapshots
│  └─ Applied Calibration / Correction Status
├─ Calibration Repository
│  ├─ Connectors / Cal Kits / Standards
│  ├─ Calibration Sessions / Standard Acquisitions
│  └─ Error Terms / Cal Sets / Validity
├─ Display Workspace
│  └─ Diagrams -> Trace Placements / Marker & Limit Overlays
├─ Processing
│  ├─ Correction / Port Extension / Fixture Operations
│  └─ Time Domain / Gating / Math / Formatting / Statistics
├─ Persistence & Interchange
│  └─ State / Calibration / Trace / Touchstone / CSV / Reports
└─ Control Surfaces
   ├─ Web UI + streaming updates
   └─ SCPI sessions + IEEE 488.2 status model over TCP Socket
```

这个地图只表达职责和依赖方向。厂商对“Measurement、Trace、Window、Diagram、Channel”的命名和绑定方式不同，不能照抄某一家的对象编号体系。

## 3. 功能能力目录

### 3.1 Instrument、状态、预置与生命周期

**Core**

- 身份：制造商、型号、序列号、软件版本、驱动版本、单板型号/固件版本；SCPI 提供稳定的 `*IDN?` 结果。
- 运行状态：启动、初始化、就绪、繁忙、等待触发、扫频中、保持、故障、关机；状态必须可由 Web、SCPI 和诊断接口一致读取。
- Preset/Reset：区分“测量预置”“恢复用户启动状态”“恢复出厂配置”。`*RST`、UI Preset 和删除持久数据不能隐式等价。
- State：保存/恢复仪器设置；至少区分仅设置、设置+校准、设置+迹线、设置+校准+迹线/Memory。普通 Recall 和异常重启默认恢复到 Hold + RF safe/off，不恢复 RF-on、Continuous/Groups、Armed/WaitingTrigger 或未完成 Operation；显式恢复运行态也必须先安全恢复，再以新的完整 admission Operation 启动。
- 当前上下文：active/selected channel、measurement、trace、diagram、marker 是协议可观察状态，不应代替稳定 ID。Web 选择是 session-local；SCPI 选择作用域由目标方言决定，可能是全机、每 Channel 共享或连接局部。
- RF 输出总开关、安全关断、异常重启后的保守默认状态。
- 配置修订号和变更来源；恢复状态必须经过当前硬件能力校验，并报告缺失端口或不支持参数，而不是静默截断。

CMT 官方说明了 State、State & Cal、State & Trace、All 等保存粒度，并指出恢复静态迹线时进入 Hold，见 [Analyzer State](https://coppermountaintech.com/help-cmtvna/1-port/analyzer-state.html)。

**Pro**

- 用户命名预置、启动自动恢复、状态比较/差异预览、只恢复选定子域、状态模板和只读模板。
- 操作审计、配方锁定、生产测试员权限与管理员权限。

**HW/Option**

- 外部测试集、开关矩阵、外部源、功率计、ECAL/ACM、Bias Tee、DC/SMU 等资源注册与状态。

### 3.2 Channel：测量作用域而非显示窗口

**Core**

- Channel 拥有扫频计划、刺激功率、IFBW、平均、触发策略、所用源端口/路由、AnalysisTrace 集合和 Correction Binding；Live AnalysisTrace 的 MeasurementSpec 表达“测什么”。
- 多 Channel 创建、复制、删除、重命名、启用/保持；每个 Channel 有独立配置修订号和测量结果代次。
- 资源仲裁：多个 Channel 共享同一源、接收机或端口时，必须由能力与调度规则决定串行、可并行或拒绝；不能假设商用品内部并发方式。
- Channel 与 Diagram 不应强绑定。R&S 允许一个 Diagram 显示不同 Channel 的迹线；Keysight 区分 Channel、Measurement 与 Window；CMT 的部分产品则更接近每 Channel 一个窗口。归一化模型应允许跨 Channel 显示，但不强迫首版 UI 暴露全部组合。
- 删除 Channel 前显式处理其 AnalysisTrace、活动扫频和校准绑定；历史不可变结果可按保留策略继续存在。

Keysight 的 [Channel Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Channel_Object.htm) 把频率、功率扫频、扫频时间和类型等设置放在 Channel；R&S 手册展示了 Trace、Diagram、Channel 的独立选择与删除行为，见 [ZNB/ZNBT User Manual PDF](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf)。

### 3.3 刺激、Sweep Plan 与扫描执行

**Core**

- 线性频率扫频：start/stop 或 center/span、点数、实际频率轴。
- 对数频率扫频：频率必须为正，实际点轴随结果返回，不能只靠 start/stop/points 重算。
- 分段扫频：有序 Segment 列表；每段至少有 enable、start、stop、points，并允许独立 power、IFBW、dwell/delay（实际支持项由能力声明）。结果保存实际拼接后的刺激轴和 Segment 边界。
- CW/Zero-span 时间扫频：固定频率，横轴为时间或采样序号；与“单个 CW 点”区分。
- 功率扫频：固定频率、start/stop power、points；上层能力存在，但只有驱动声明可编程功率及合法范围时才启用真实执行。
- sweep mode：continuous、hold、single、groups/count；abort、timeout、取消和重新触发具有明确终态。
- sweep sequence：按源端口整扫或逐点切换的差别必须进入扫频计划/能力模型，因为多端口校准和速度会受影响。
- 扫频时间、每点 dwell、扫频前 delay、自动/手动 sweep time；驱动必须返回最终采用值，不能假设请求值被精确接受。
- 每次执行生成独立 `sweep_id`，绑定配置修订、刺激实际轴、端口路由、校准版本和完成状态。

Keysight 官方列出 Linear、Log、Power、CW Time、Segment 和 Phase 等类型，并说明 Segment 可独立配置功率、IFBW、扫频时间、dwell 和 delay，见 [Sweep Settings](https://helpfiles.keysight.com/csg/N52xxB/S1_Settings/Sweep.htm) 与 [Sweep SCPI](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Sense/Sweep_SCPI.htm)。这说明“扫频”应建模为有类型的计划，不能退化成三个频率字段。

**Pro**

- 任意/反向/重叠 Segment、非均匀点表；必须定义标记、限制线、群时延和校准插值在非单调轴上的行为。
- Phase sweep、频率偏置（FOM）、外部源列表扫频、二维扫频、功率压缩等专用 Sweep Plan。
- 生产序列/多 Channel group trigger、按 Segment 触发、按点触发。

**HW/Option**

- Fast CW、高速 FIFO、脉冲/门控接收、相位可控多源、毫米波变频、混频器/频率转换扫频、IMD、噪声、频谱分析等。Keysight 高端 PNA/PNA-X 将许多此类能力作为专用测量类或选件，不能列为基础 VNA 承诺。

### 3.4 Source、Receiver、Port、IFBW、Average 与 Trigger

**Core**

- 物理端口与逻辑端口分开；能力返回端口数量、名称、可作为源/接收的方向、频率范围、功率范围、阻抗及可用路由。
- Source：RF on/off、目标功率、端口功率耦合/解耦、功率范围检查；真实输出值和未锁定/未稳幅状态可诊断。
- Receiver：参考接收路径 `aN` 与测试接收路径 `bN` 的可用性、饱和/过载/无效点状态；通用适配接口交付逐频点复数相量和采集质量，不包含 ADC/IQ 时域处理。
- IFBW：Channel 默认值；若硬件支持，Segment/port override 通过能力发现开放。驱动返回可选值或 min/max/步进规则。
- Average：on/off、factor/window、clear/restart、当前完成计数；至少支持复数平均。策略必须正交声明输入 stage（receiver waves、measured ratio、factory-corrected ratio 或 corrected network）、sample boundary（point、source state 或 logical sweep）和 typed mode（FiniteBatch、SlidingWindow、Cumulative、VendorRunning）。SlidingWindow 使用预留固定 contribution ring 保存逐点复数 contribution/weight/quality，不依赖 A 层 retention；VendorRunning 只有在 Compatibility Profile 冻结 update-kernel/state schema 后才开放。Keysight 公开流程是 ratio（ENA 可含端口/工厂特性修正）→sweep average→用户 error correction；CMT 内部数组资料是 receiver average→ratio→用户 correction，但需按目标固件回归。不能平均显示格式，也不能隐式双重平均。平均完成与单次 sweep 完成是不同事件。
- Trigger：internal/free-run、manual/bus、external；scope（global/channel）、event（channel/sweep/point/segment）、delay、polarity/edge、等待触发状态、trigger out（若有）。
- `single`/`INIT` 只表示发起执行；结果读取、Marker 搜索、Limit 判定必须等待绑定的完整执行完成。

官方依据：[Keysight Avg/BW Commands](https://helpfiles.keysight.com/csg/N52xxB/Programming/CF_Avg_BW_Commands.htm)、[Keysight Trigger Modes](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Sense/Sweep_SCPI.htm)、[CMT Trigger Command Tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/trigger.html)。三家都提供比“点一下开始扫频”更完整的触发模型。

**Pro**

- Sweep/point averaging 类型、平均触发、移动平均；触发输入/输出脉冲宽度和端口映射；触发序列可视化。

**HW/Option**

- Source/receiver attenuator、receiver leveling、外部参考时钟、外部源、独立源输出、直通接收机、Bias/DC/SMU、Handler I/O；这些参数只在相应硬件路径存在时可见。

### 3.5 Measurement Spec、Analysis Trace 与多端口/混合模

**Core**

- MeasurementSpec 是“测什么”的值对象，不是显示曲线或独立 CRUD 聚合。至少支持单端 S 参数 `S(out,in)`；端口数由驱动能力决定，不把软件写死为 2 端口。
- 反射/传输、正向/反向测量；一个 Channel 可包含多个 AnalysisTrace，其 Live Source 内含 MeasurementSpec，并由采集计划合并所需源端口和接收路径。
- ratioed receiver（例如 `b2/a1`）和 unratioed receiver（例如 `a1`、`b2`）作为高级/诊断测量定义；原始波量不能与经校准 S 参数混称。
- AnalysisTrace 具有稳定 ID、名称、Source Spec、处理投影、Marker/Limit/Memory 关系和可用状态；MeasurementSpec 作为值对象随 Trace revision 版本化。
- 常用派生量/显示格式：complex、real、imaginary、linear magnitude、log magnitude、phase、unwrapped phase、group delay、SWR、Smith（阻抗/导纳）、polar。格式不改变底层复数网络/Trace 输入快照。

Keysight 明确区分预定义 S 参数、任意接收机比值、未比值绝对接收机数据和 balanced measurement，见 [Measurement Parameters](https://helpfiles.keysight.com/csg/N52xxB/S1_Settings/measurement_parameters.htm)。

**Pro**

- 物理端口到逻辑端口映射；balanced port topology；mixed-mode `Sdd/Sdc/Scd/Scc`；single-ended/mixed-mode 互换；端口阻抗和 wave definition 明确化。
- 参数转换：Z/Y/H/G/T/ABCD 等网络参数、稳定性因子、增益等派生分析；每一种都声明输入端口数和数值有效域。
- Equation/自定义 Measurement，只能引用有明确刺激轴对齐规则的数据。

R&S 和 Keysight 都支持混合模，但拓扑、命名和选件范围不同，见 [R&S ZNB/ZNBT User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf) 与 [Keysight Balanced Measurements](https://helpfiles.keysight.com/csg/NA520xA/S1_Settings/Balanced_Measurements.htm)。

**HW/Option**

- 真正的差分激励、多个相干源、任意接收机访问、绝对功率的可溯源精度、噪声/频谱/IMD/混频器测量类。仅靠普通单源多端口 `a/b` 数据可计算 mixed-mode 结果，但不能据此宣称具备 true-mode stimulus。

### 3.6 数据层级、结果快照与数据质量

**Core（非功能性底线）**

必须分开保存或显式表示以下层级，禁止用一个可变数组在流程中原地改写：

1. `AcquisitionBlock`：实际刺激轴、源/接收路径、逐点 `a/b` 复数相量、时间戳、顺序、质量标志；
2. `UncorrectedMeasurement`：由波量形成的未校准比值或未比值测量；
3. `CalStandardAcquisition`：标准件采集原始数据，属于校准会话而非 DUT 历史；
4. `ErrorTermSet / CalSet`：求解出的误差项及其适用条件；
5. `CorrectedMeasurement`：应用误差修正后的复数网络结果；
6. `ProcessedMeasurement`：端口延伸、夹具、混合模、时域/门控、迹线数学等处理结果；
7. `FormattedTraceData`：LogMag、Phase、Smith 等显示数据；
8. `AnalysisResult`：Marker、带宽、统计、Limit pass/fail；
9. `CompletedSweepBundle`：一次完整逻辑采集原子发布的 A 层 receiver-observation 集合；`CompletedMeasurementBundle` 是完成 RF/平均/校准处理后的 B 层正式网络结果；从 A/B 惰性物化的 receiver/ratio/corrected/ProcessedNetwork 使用不带 Trace/Marker/Limit revision 的 `MeasurementStageSnapshot`；`AnalysisPublication` 才是逐 Trace 的 C 层求值。Live 路径为 A→B→C 且失败隔离；Frozen/Imported/Derived C 使用 typed `AnalysisInputRefSet` 引用静态、导入或一个/多个上游 C，不伪造 A/B。

预览数据可按点/块流向 Web，但失败、取消或未完成预览不得提升为正式快照。Marker/Limit 固定 C 层 publication；Touchstone/网络数据固定 B 或 `MeasurementStageSnapshot(ProcessedNetwork)`；receiver 数据固定 A/B stage；CSV 按 `data_stage` 固定 B/stage/C。非 C 层缺少物化结果时启动/共享 `MaterializeMeasurementStageOperation`，不能伪造 AnalysisTrace。SCPI/保存命令必须显式映射上述 typed result，等待命令绑定具体 Operation/QueryTicket，而不是笼统读取“最近一次扫频”。

正式结果按层携带最小 provenance：A 含 `LogicalSweepId + BoardRunEvidence[]`，逐板绑定 Manifest、BoardRun generation、完成账本、identity/capability、实际刺激轴、端口/路由和采集质量，默认单板时数组长度为 1；B 含 `measurement_snapshot_id`、有界 `AverageContributionRef`、平均/CorrectionSet/MatchReport、网络阶段和质量；惰性 stage 含父 A/B refs、requested stage、RF/network graph、axis/topology/Z0/quality；C 含 `analysis_publication_id`、AnalysisInputRefSet、Trace/处理链/Marker/Limit revision 和分析质量。Finite Average 的 contribution 可显式保存受 factor 上限约束的 Sweep IDs；Sliding 保存有界窗口 IDs、固定 contribution ring 的结构共享引用与 accumulator snapshot；Cumulative/VendorRunning 保存 accumulator snapshot ID、generation/count、首末序号范围和滚动强摘要，逐 Sweep 细节只进入有界审计 retention。各层都带软件/schema/Profile revision 与完成时间；非 Live C 不强制携带 sweep 或 CalSet。修改当前配置不能追溯性改变历史结果的含义。

Keysight 的数据访问图明确区分接收机复数数据、比值数据以及后续访问点，见 [Accessing Data](https://helpfiles.keysight.com/csg/N52xxB/Programming/Accessing_Data_Descriptions.htm)；CMT 也在命令树中分开 corrected data、formatted data、calibration measurements 与 coefficients，见 [CMT CALCulate Commands](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html)。

### 3.7 Display Workspace、Diagram 与 Trace

**Core**

- `DisplayWorkspace`：浏览器中的整体布局和用户显示状态。
- `Diagram`：一个绘图区；拥有坐标系、网格、轴、标题、legend、缩放、参考位置、迹线叠放顺序及布局位置。它不拥有采集和校准状态。
- 厂商 `Trace` 是多义外部术语：Keysight 的显示 Trace 更接近视图，R&S/CMT 的 Trace 还承载 measured quantity、format、Marker 和 Limit。本项目内部使用 `AnalysisTrace(TraceSourceSpec)` 表示可分析对象，使用 `TracePlacement` 表示 Diagram 内的颜色、线型、可见性、scale/div、reference position 和 z-order。
- 多条 AnalysisTrace 可以包含等价 MeasurementSpec 并以幅度、相位、Smith 等不同设置显示；是否允许一条 AnalysisTrace 同时有多个 Placement 由 Product/Compatibility Profile 限制。删除 Placement、AnalysisTrace、Diagram 和 Channel 是不同核心 Command，厂商删除副作用另行映射。
- 一个 Diagram 可叠加多个 Trace，但必须检查 X 轴域、单位、坐标类型和结果代次兼容性。Smith/Polar 与 Cartesian 的 overlay 规则要显式。
- Diagram layout：单图、多图、网格、最大化；active diagram/trace 是 UI 选择，不是所有权。
- 增量预览与正式快照有视觉状态区别；Web 重连后能用快照+修订恢复，而不是要求重扫。

厂商差异：R&S 使用 Diagram；Keysight 更常使用 Window；CMT 部分产品把 Channel Window 与 Trace 布局结合。项目核心统一使用 `Diagram`，前端绘图库内部叫 Chart 不进入领域模型。CMT 的显示命令包含布局、Trace 显示和缩放，见 [CMT DISPLAY Commands](https://coppermountaintech.com/help-cmtvna/Programming-Manual/display.html)。

**Pro**

- 双 Y 轴、共享轴/联动缩放、trace coupling、用户注释、截图/报告模板、多工作区。
- 多次历史快照 overlay、Golden/Reference trace、批次颜色规则。

### 3.8 Marker：读取、搜索与分析对象

**Core**

- Marker 绑定一个 AnalysisTrace 及其完整结果代次；至少含 stable ID/number、on/off、stimulus、实际落点、响应值和读数格式。Keysight 兼容层以 Measurement 寻址，R&S/CMT 兼容层以 Trace 寻址。
- normal marker、reference marker、delta marker；delta 明确引用哪个参考 Marker，并同时报告 ΔX/ΔY。
- discrete（吸附采样点）与 interpolated 模式；插值规则和非法点处理可测试。
- peak/max/min、next peak、target/transition 搜索；支持峰值 polarity、excursion/threshold 和 search range/domain。
- search tracking：每次新完整快照后重新搜索，而不是把旧 index 搬到新数组；可暂停并保留最后结果。
- marker coupling：多个 Trace/Diagram 共享 X 位置时需检查 X 轴兼容性。
- bandwidth/notch/filter search：至少给出左右交点、带宽、中心频率、Q、插入损耗/峰值；找不到一侧交点时返回结构化“不完整”，不能伪造 0。
- marker-to 操作：由 Marker 设置 start、stop、center、span、reference level、electrical delay 等，但必须作为正常配置命令进入同一冲突和审计流程；刺激范围目标是 Channel，electrical delay 目标是 AnalysisTrace，reference level/position 目标是明确 TracePlacement，多 Placement 歧义不能静默选择。
- Marker 读数只来自已完成快照；扫频预览上的游标可作为单独 UI cursor，不能冒充正式 Marker 结果。

Keysight 的 Marker 支持带宽搜索，返回 bandwidth、center、Q、loss，见 [Marker SCPI](https://helpfiles.keysight.com/csg/pxivna/Programming/GP-IB_Command_Finder/Calculate/Marker.htm)；CMT 公开了 reference/delta、search domain、peak excursion/polarity、target、tracking、bandwidth、flatness 等命令，见 [CMT CALCulate Commands](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html)；R&S 手册还区分 normal、delta、fixed、discrete 等 Marker 行为。

**Pro**

- 多区间统计 Marker、flatness、ripple、峰表、自动标注所有峰、marker table 导出、跨 Trace marker link。

### 3.9 Limit Line、Limit Test 与生产判定

**Core**

- `LimitTest` 与 `LimitLine/Segment` 分离：一个测试包含有序的 upper、lower、single-point/off Segment；每段有起止刺激与起止限值。
- Limit 绑定 AnalysisTrace 的 projection/格式/单位和结果代次。修改格式时必须转换、拒绝或使 Limit 失效，不能继续用旧单位静默判断；Diagram 只保存 Limit Line 的 Placement/可见性。
- 判断基于实际测量点；是否判断插值线段必须固定。Keysight/CMT 基本语义是逐点判断。
- 输出：整体 pass/fail、失败点数量、失败点刺激和值、对应 upper/lower、最大裕量/最差点；显示开关与测试开关分离。
- Channel/Instrument 聚合状态：只聚合本次指定执行中的测试，不能让陈旧失败状态污染新批次。
- Limit 定义导入/导出、版本、名称、单位、创建者/时间；生产测试保存判定时同时保存所依据的结果和 Limit 版本。
- Web、SCPI 使用同一判定服务；SCPI 至少可设置表、开关、查询 fail、失败点/报告。

Keysight 的 Limit 命令支持 max/min Segment、总 Fail 和逐点报告，见 [CALCulate:MEASure:LIMit](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Calculate/MeasureLIMit.htm)；CMT 同样公开 limit table、failed points 和报告，见 [CALC:LIM:DATA](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calclimdata.html)。

**Pro**

- Ripple limit、peak limit、margin line、频带模板、连续 N 次通过/失败、handler 输出、蜂鸣/告警、批量报告。Ripple 在 Keysight 和 CMT 均有专门命令，但不应挤占首版常规 upper/lower limit 的交付质量，见 [Keysight Ripple Limit](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Calculate/MeasureRLIMit.htm)。

### 3.10 Trace Math、Memory、Hold、Smoothing 与 Statistics

**Core**

- `Data -> Memory` 保存不可变复数数据、刺激轴、来源快照和单位；Memory 不是当前数据数组的别名。
- data、memory、data+memory、data-memory、data*memory、data/memory；复数运算在格式化之前执行。
- 刺激轴不一致时只允许明确的 exact/interpolated/rejected 结果；默认不外推，状态可见。
- Min/Max hold、清除 hold；smoothing 的 aperture 和边界规则；二者的处理顺序固定并记录。
- 全跨度或指定区间的 mean、standard deviation、peak-to-peak、min/max；统计使用哪个数据层和格式必须声明。
- 静态/locked trace 用于比较历史 DUT；与 Memory buffer 的生命周期区分。

Keysight 明确说明 Trace Math 在格式化前对复数数据执行，并提供 Data/Memory 四则运算以及 mean、standard deviation、peak-to-peak，见 [Math Operations](https://helpfiles.keysight.com/csg/N52xxB/S4_Collect/Math_Operations.htm)。

**Pro**

- 多 Memory 槽、Equation Editor、跨 Channel 数学、曲线拟合/去趋势、uncertainty overlay、RF filter statistics。

### 3.11 Time Domain 与 Gating

**Pro（首版架构预留，核心功能完成后交付）**

- frequency-to-time transform：band-pass impulse、low-pass impulse、low-pass step。
- Window：minimum/normal/maximum/user-defined，至少保留 window 参数和所形成的分辨率/旁瓣权衡。
- low-pass harmonic grid、DC extrapolation/known DC、time/distance、velocity factor、one-way/round-trip。
- gating：band-pass 或 notch gate；start/stop 或 center/span、gate shape；输出回到频域的 gated complex result，不能只在图上遮挡。
- Marker、Limit、保存和 SCPI 能明确选择原频域、时域或 gated 结果；处理链与单位进入结果元数据。

CMT 官方列出 Bandpass、Lowpass Impulse、Lowpass Step、谐波网格、DC 外推和 Kaiser window，见 [Time Domain Transformation](https://coppermountaintech.com/help-cmtvna/1-port/time-domain-transformation.html)；Keysight 对应命令同时提供 transform 和 gating，见 [Keysight Time Domain Commands](https://helpfiles.keysight.com/csg/N52xxB/Programming/CF_Math_Commands.htm)。

**厂商差异与限制**

- CMT 多数产品把时域/门控作为软件标准能力，但部分紧凑型号例外；Keysight、R&S 的可用范围随型号/选件变化。故本项目应以软件专业扩展呈现，并由数据轴和板卡频率能力校验，而不是默认所有单板都可提供有效 TDR 结果。
- 不得把增强型 TDR、眼图、阻抗剖面、自动夹具移除等高端选件等同于基础逆变换和门控。

### 3.12 Port Extension、Embedding、De-embedding 与 Fixture

**Core**

- Port extension：按端口设置 electrical delay；若支持损耗模型，应显式包含 loss/frequency/velocity 参数。它只适合近似匹配传输线，不能被描述为任意夹具去除。
- Correction plane 和 DUT plane 的名称、端口映射、处理启用状态进入结果元数据。
- 处理链顺序必须公开并可测试；不能从厂商外部命令推断其闭源内部顺序。

**Pro**

- 基于 Touchstone `s2p/sNp` 的每端口或多端口 de-embedding/embedding、级联、反转、port Z conversion、matching circuit、adapter removal/insertion。
- 夹具网络的端口数、方向、参考阻抗、频率覆盖和 passivity/causality/conditioning 警告；频率不覆盖时默认拒绝或明确降级，不能静默外推。
- Fixture 配置有 ID/版本；所用 Touchstone 内容摘要与结果绑定，原文件后来被覆盖不得改变历史含义。

CMT 的命令树列出 port extension、fixture simulator、embedding/de-embedding 和 port impedance conversion，见 [CMT SCPI Command Tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/scpi-command-tree.html)；Keysight ENA 资料明确区分 port extension 与任意二端口网络去嵌，见 [Fixture Simulator Overview](https://helpfiles.keysight.com/csg/e5072a/measurement/fixture_simulator/overview_of_fixture_simulator.htm)。

**HW/Option**

- 自动夹具移除（AFR）、多级夹具表征、in-situ de-embedding、探针台/开关矩阵路径管理等。

### 3.13 Calibration：完整生命周期而非开关

校准是首版成熟核心的主干，至少包含以下对象和状态。

#### 3.13.1 Cal Kit、Connector 与 Standard（Core）

- Connector：接口族、gender、reference impedance、频率范围。
- Cal Kit：稳定 ID、名称、版本、支持频段、连接器、标准件集合和 class assignment。
- Cal Standard：Open、Short、Load、Thru、Reflect、Line 等类别；定义可以是参数模型或表格/Touchstone 数据，并记录频率覆盖、delay/loss、阻抗、序列号和修订。
- 标准件“物理实例”与“模型定义”分离，避免替换模型后历史校准失去可追溯性。

CMT 对 SOLT/TRL standard class 和 class assignment 有明确说明，见 [Classes of Calibration Standards](https://coppermountaintech.com/help-cmtvna/1-port/classes-of-calibration-standar.html)。

#### 3.13.2 Calibration Method（Core 与 Pro）

**Core**

- Reflection response/normalization、Transmission response/normalization；
- Full one-port SOL；
- One-path two-port；
- Full two-port SOLT，含可选 isolation acquisition；
- 方法声明所需端口、方向、标准步骤、可修正的 Measurement 类型和误差项集合。

**Pro**

- Unknown Thru/SOLR、TRL/LRL、TRM/LRM、adapter removal、multiport subset/full N-port、sliding load、waveguide variants。

CMT 官方表格列出 response、full one-port、one-path two-port、full two-port 与 TRL 的标准件和误差项，见 [Calibration Methods and Procedures](https://coppermountaintech.com/help-cmtvna/1-port/calibration-methods.html)。厂商方法命名不同，例如 R&S 常用 TOSM/UOSM/TRL/TOM；项目应保存规范化 method kind，同时允许 SCPI 兼容层使用目标方言别名。

#### 3.13.3 Calibration Session（Core）

- Guided/unguided 会话状态机：configured、waiting-for-standard、acquiring、step-complete、ready-to-solve、solving、completed、aborted、failed。
- 会话冻结 ports、method、stimulus、power、IFBW、routing、cal kit/version 和 board identity；中途配置改变必须拒绝或显式重启。
- 每一步有 expected standard/ports、实际采集、质量检查、repeat/skip（仅允许的步骤）、时间、操作者；支持 abort，不能把不完整步骤保存为有效 CalSet。
- 标准件原始采集 `CalStandardAcquisition` 与求解的误差项分开保存。Keysight 官方同样区分 standard measurement data 与 error terms，见 [Read and Write Calibration Data](https://helpfiles.keysight.com/csg/N52xxB/Programming/Learning_about_COM/Read_and_Write_Calibration_Data_using_COM.htm)。

#### 3.13.4 Error Terms、Cal Set 与应用（Core）

- 求解产生不可变、版本化的 `CalSet/CorrectionSet`；包含方法、端口、误差项、实际 X 轴、功率/IFBW/attenuator/path、kit/standard 版本、时间、温度（若有）、板卡身份和算法版本。
- Channel 只引用所应用的 CalSet 版本；重新校准产生新版本，不原地覆盖正在被结果引用的数据。
- 支持 exact match、interpolated、changed/degraded、disabled、missing、rejected 等 correction status，并在 Trace/Channel/Web/SCPI 一致显示。
- 插值仅在校准频率覆盖内且轴可合理插值时执行；外推默认拒绝。厂商差异必须保留：Keysight 对普通 S 参数超出校准起止范围通常关闭修正，而 CMT UI 公开 `C!` extrapolation 状态；项目不能静默选择其中一种。
- 修改 start/stop/points、power、IFBW、attenuator、routing、sweep type 时分别评估 exact/interpolated/degraded/rejected，不能用一个 `calibration_enabled` 布尔值覆盖。
- 每个 B 层 `CompletedMeasurementBundle` 记录实际应用的 Correction Set、MatchReport 和源 `CompletedSweepBundle` ID；A 层采集事实不伪装成已应用用户校准。

Keysight 的 [Error Correction and Interpolation](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Error_Correction_and_Interpolation.htm) 说明了 full N-port、response、interpolated 和 changed settings 等状态；[CSET Commands](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/CSET.htm) 提供 CalSet 属性、误差项和有效性查询。

#### 3.13.5 Calibration Verification / Confidence Check（Pro）

- 校准验证是 solve/apply 之后的独立工作流，不是重新校准、SelfTest 或 Correction Set 状态位。
- `VerificationPlanRevision` 固定 Correction Set、独立 verification artifact 的 characterization、端口/实际轴、所需 S 参数、tolerance/uncertainty 和算法版本；执行使用正式 B 层测量。
- 结果是不可变 `CalibrationVerificationResultSnapshot`，给出逐点 residual/margin 和 Pass/Fail/Indeterminate；失败/取消不修改 Correction Set/Binding，Pass 只对该 Plan 和测量系统组合有效。
- 不得把求解时同源标准件数据冒充独立验证，也不得把 system/confidence check 宣称为单个仪器或标准件认证。

Keysight [System Verification](https://helpfiles.keysight.com/csg/m9485a/support/system_verification.htm) 比较四个 S 参数测量、verification device 工厂数据和 uncertainty limits，并明确 PASS 是 analyzer/cable/adapter 系统级结论而非部件认证；CMT [Confidence Check](https://coppermountaintech.com/help-cmtvna/1-port/confidence-check2.html) 比较当前校准测得的内部衰减器 S 参数与模块内存表征。

#### 3.13.6 自动校准与功率校准（HW/Option）

- ECal/ACM：模块发现、端口映射、模块身份/特性数据、guided steps、失败恢复；没有模块时能力为 unavailable。
- Source power calibration：功率计、频率/功率范围、loss offset、source correction table。
- Receiver power calibration：绝对/比值 receiver 测量的修正与适用路径。
- 功率校准和 S 参数校准不能混成同一组误差项或一个布尔状态。
- Scalar/Vector mixer calibration、noise calibration 等属于相应专业测量类，不进入基础 SOLT 交付承诺。

Keysight 的 Cal All 流程显式包含 connector、cal kit、guided session、ECal、source/receiver power calibration 和保存 CalSet，见 [Perform a CalAllChannels Calibration](https://helpfiles.keysight.com/csg/N52xxB/Programming/GPIB_Example_Programs/Perform_a_CalAllChannels_Calibration.htm)。

### 3.14 文件、Touchstone、状态与报告

**Core**

- 保存/恢复 Instrument State；可选包含 CalSet、data trace、Memory，格式必须带 schema/version 和硬件能力快照。
- Touchstone `s1p/s2p/.../sNp` 导入/导出：端口顺序、reference impedance、频率单位、RI/MA/DB、注释和版本明确；导出来自同一完整结果代次。
- CSV/TSV：实际 X 轴、复数或格式化 Y、单位、measurement/trace metadata；不得只输出屏幕抽样点。
- Calibration kit/standard、CalSet、Limit table、Segment table、Fixture/Touchstone、静态 Trace 的独立文件类型。
- 原子写入、校验失败不替换旧文件；路径白名单、文件名校验、空间不足和只读介质错误可报告。
- Web 下载和 SCPI `MMEMory` 访问遵循同一权限、文件根和审计规则。

CMT 的 `MMEMory` 命令覆盖 state 和 Touchstone，见 [MMEMory Commands](https://coppermountaintech.com/help-cmtvna/Programming-Manual/mmemory.html)；R&S ZNB 手册支持多 Trace 数据、Memory 和 Touchstone 文件。不同厂商的私有 state/cal 文件不互通，本项目应定义自己的版本化格式，仅把 Touchstone/CSV 作为跨产品数据交换格式。

**Pro**

- 报告模板、PDF/图片、批次结果包、签名/校验和、状态迁移工具、旧 schema 升级与只读预览。

### 3.15 SCPI、IEEE 488.2 队列与状态模型

**Core**

- TCP Socket 上的 SCPI parser：大小写不敏感、长短关键字、层级、数字/单位、字符串、命令串、query、行终止、ASCII 与 IEEE definite-length binary block。
- 明确的兼容方言。Keysight、R&S、CMT 命令树并不完全相同；首版不能声称三家全兼容。应选定一个主方言或定义项目方言+明确兼容子集，并提供 capability/version query。
- 每个 TCP 连接有独立 parser、输入输出、响应顺序和连接生命周期；selected channel/measurement/trace 的作用域由 Compatibility Profile 决定，不能硬编码为连接局部。仪器配置、正式数据与硬件资源始终共享。
- 命令按接收顺序解析；query response 按顺序进入 output queue。未读 response 遇到下一 query 的行为必须固定，并产生 query error，而不是覆盖旧响应。
- 有限 FIFO error queue：区分 command、execution、device-specific、query error；`SYST:ERR?` 读取并弹出，空队列返回 no error，溢出策略固定。
- IEEE 488.2 基础：`*IDN?`、`*RST`、`*CLS`、`*OPC`、`*OPC?`、`*WAI`、`*ESE/*ESE?`、`*ESR?`、`*SRE/*SRE?`、`*STB?`、`*TST?`（真实自检范围明确）。
- Standard Event Status、Status Byte、Operation/Questionable 状态；operation complete 绑定 pending operation，而不是“命令已入队”。
- `ABORt`、single/initiate、continuous/hold 与等待触发可通过状态查询区分。
- 大数据 query 支持 binary block 和 backpressure；连接断开取消等待者，但是否取消底层 sweep 由命令语义决定，不能默认杀掉共享测量。

CMT 说明 SCPI 可经 HiSLIP 或 TCP/IP Socket 发送，见 [CMT Programming](https://coppermountaintech.com/help-cmtvna/Programming-Manual/programming.html)；其 `SYST:ERR?` 是读取即弹出的 FIFO，见 [CMT Error Queue](https://coppermountaintech.com/help-r/systerr_.html)。R&S 官方说明 `*OPC?`、STB polling 和 SRQ 的同步差异，见 [Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html)。

**传输限制**

- 基础单连接 raw TCP Profile 只承诺寄存器与 `*STB?` polling。R&S RawSocket 没有 VISA control channel，但 Keysight PNA 公开了可接收 SRQ/Device Clear 的专有第二 TCP control connection；因此 PNA control socket、HiSLIP/VXI-11 或项目事件协议必须作为独立 Transport capability，不能把所有 raw Socket 一概宣称“有 SRQ”或“无 SRQ”。

**Pro**

- HiSLIP/VXI-11、SCPI command logging/replay、厂商兼容测试包、多连接控制锁、异步事件通道。

### 3.16 Web UI 与远程控制

Web 是本项目明确要求的主操作面，但不是三家 VNA 的统一领域标准。商用品可能使用本地 GUI、VNC/远程桌面、Web remote front panel 或独立自动化接口；SCPI 才是更稳定的跨产品自动化形态。因此不能根据厂商网页外观推断内部架构。

**Core**

- Web 与 SCPI 调用同一应用命令和查询层，不能各自实现一套校准、Marker、Limit 或状态规则。
- 完整页面：Instrument/health、Channel/stimulus、Measurement、Calibration wizard/repository、Display/Diagram、Marker、Limit、File/State、SCPI/remote settings、diagnostics/logs。
- 浏览器初次连接获取一致快照，之后按修订增量更新；断线重连、慢客户端、丢包和多标签页不破坏仪器状态。
- 扫频 preview 分块推送并限速/抽稀，CompletedSnapshot 原子发布；前端绘图抽稀不能改变导出/Marker/Limit 使用的全分辨率数据。
- 多用户并发写需要控制权/租约或明确的 optimistic concurrency；冲突返回当前修订和原因。SCPI 与 Web 不得静默互相覆盖一组正在编辑的 Segment/Cal Session。
- 登录、密码变更、会话超时、只读/控制权限、CSRF/Origin 检查、TLS 终止部署说明；在封闭网内也不能默认所有浏览器都有管理员权。
- 所有长操作都有 progress、cancel、timeout 和结构化错误；Web 页面刷新不取消共享 sweep/calibration。

CMT 已提供内建 Socket server 和无硬件 Demo Mode，见 [Connection Setup](https://coppermountaintech.com/help-cmtvna/Programming-Manual/connection-setup.html) 与 [CMT VNA Software](https://coppermountaintech.com/help-cmtvna/Programming-Manual/index.html)。这支持本项目把真实驱动与 Mock 驱动做成同一上层行为，但不证明其内部实现方式。

**Pro**

- 多用户角色、审计导出、远程协作只读链接、生产看板、批处理/配方执行、浏览器端离线报告查看。

### 3.17 诊断、自检、日志与能力发现

**Core**

- Capability manifest 是驱动适配层的必选输出：端口/源/接收机数量与拓扑、频率/功率范围、支持的 IFBW、最大点数/Segment、可用 sweep/trigger、是否支持 per-segment overrides、可取得的 source/receiver/auxiliary observations、外设、并行限制、Clock/Coherence Domain、timebase lock、同步 trigger/epoch、最大 skew、out-of-band abort 与 RF safe-state 能力。它不直接宣称用户校准方法；最终 `CalibrationMethodCapability` 由 `BoardCapabilities × ProductProfile × CalibrationModule` 推导。未知相干能力不得把多块板合成同一代网络矩阵。
- 能力值区分 supported、unsupported、temporarily unavailable 和 unknown；配置验证返回具体约束，不靠 UI 隐藏兜底。
- 启动自检：驱动加载、板卡连接、固件兼容、参考锁定、源/接收路径基本状态、存储和网络；`*TST?` 只报告实际执行过的范围。
- 在线 health：温度/电压/锁定/过载等仅在板卡上报时存在；通信延迟、丢块、重试、最后成功 sweep、队列深度属于上层通用健康指标。
- 结构化日志：时间、severity、component、operation/sweep/session ID、错误码；敏感信息脱敏、环形保留、空间上限和下载诊断包。
- 用户错误、SCPI error queue、设备故障、内部日志分层；不能把所有异常都塞进一个字符串队列。
- Mock 驱动使用同一 capability schema，可配置端口数、频率范围、噪声、DUT 模型、延迟、触发等待、过载和故障注入；它验证上层语义，但不证明真实射频精度。

Keysight 提供频率、端口、源、接收机、IFBW、点数和选件等 capability 查询，见 [SYSTem:CAPability](https://helpfiles.keysight.com/csg/N52xxA/Programming/GP-IB_Command_Finder/SystCapability.htm)；CMT `SERVice` 同样提供端口及活动对象等能力查询。Keysight 还把 analyzer/OS errors 记录到错误日志，见 [About Error Messages](https://helpfiles.keysight.com/csg/N52xxA/Support/About_Error_Messages.htm)。

**Pro**

- 诊断包、性能计数器、趋势/健康历史、远程支持授权窗口、校准到期/漂移提醒、板卡固件升级协调。

**HW/Option**

- 厂商服务校准、内置环回、自校准路径、风扇/温度/电源传感器、硬件详细自测。适配层只能暴露真实存在的项目。

## 4. 首版成熟核心的验收边界

首版不能只完成“扫一条 S11 并在网页画出来”。达到成熟核心至少应形成以下闭环：

1. **仪器闭环**：启动、能力发现、Preset、RF 安全状态、State 保存/恢复、诊断和 Mock/真实驱动切换。
2. **测量闭环**：多 Channel；linear/log/segment/CW-time/power（按能力）Sweep Plan；source/receiver/IFBW/average/trigger；single/continuous/hold/abort。
3. **数据闭环**：逐点/分块预览、完整快照、真实刺激轴、质量标志、不可变历史和可追溯处理链。
4. **参数闭环**：多端口单端 S 参数、ratioed/unratioed receiver、常见复数显示格式和 group delay。
5. **校准闭环**：connector/kit/standard、guided session、response/full one-port/one-path/full two-port SOLT、标准件采集、误差项、CalSet、应用/插值/失效状态，以及使用独立已表征 artifact 的校准验证结果。
6. **分析闭环**：Diagram/Trace、完整 Marker 搜索与带宽、upper/lower Limit 和报告、Memory/math/hold/smoothing/statistics。
7. **交互闭环**：Web 全功能操作和实时状态；Socket SCPI、错误/输出队列、IEEE 488.2 状态与完成同步；两者使用同一规则。
8. **文件闭环**：State、Calibration、Touchstone、CSV、Limit/Segment/Fixture 定义的版本化保存、恢复与错误处理。
9. **验证闭环**：Mock 可模拟至少一个 2-port DUT、噪声/延迟/触发/故障；端到端测试能证明 Web 与 SCPI 对同一命令得到同一结果代次。

Time Domain/Gating、完整 de-embedding、mixed-mode、TRL/Unknown-Thru、Equation Editor 和高级生产判定属于下一层专业扩展，但核心数据模型、处理链和文件格式必须从首版起允许它们加入，而无需推翻 Channel、CompletedMeasurementBundle、Correction Set 或 AnalysisTrace 的身份模型。

## 5. 明确不作为基础能力冒充的高端项目

以下能力在商用高端 VNA 中真实存在，但通常依赖型号、硬件路径或付费应用，当前只能列入 HW/Option 路线：

- Noise figure/noise parameter、Spectrum Analyzer、phase noise；
- scalar/vector mixer、frequency converter、embedded LO；
- gain compression、IMD、modulation distortion、N-tone、DPD；
- pulse profile、point-in-pulse、fast CW/FIFO；
- true-mode balanced stimulus、多相干源相位扫描；
- enhanced TDR、eye diagram、jitter/mask、automatic fixture removal；
- mmWave extender、外部 test set/switch matrix、DC/SMU 同步测量；
- 不确定度分析、计量级验证/校准更新系统。

接口层应能声明并承载这些能力，但首版 Web/SCPI 不应为未实现项目放置可操作命令，更不能由 Mock 的存在推断真实单板支持。

## 6. 厂商差异与待项目决策

下列事项没有单一“商用标准答案”，必须在后续规格中明确，而不是继续靠口头默认：

1. 首版 SCPI 主方言及兼容范围；对象编号是 channel/measurement/trace/diagram 哪种映射。
2. Web 与多 SCPI 会话同时写配置时的控制权、冲突和审计规则。
3. 多 Channel 共享单板资源时的调度与公平性；哪些板卡可并行。
4. 完整数据处理图及每一步顺序，尤其 calibration、port extension、fixture、mixed-mode、time gate、math、smoothing。
5. 校准算法和首版精度验收方法；支持的 SOLT/TRL 误差模型不能只由命令名决定。
6. CalSet 插值、外推和失效的保守策略；建议普通 S 参数默认不超出校准频率覆盖外推。
7. 正式快照数量、内存上限、Web preview 限速、历史结果和文件保留策略。
8. Limit 判断的边界包含关系、无效点政策、NaN/过载是 fail、skip 还是 indeterminate。
9. Marker 插值、峰相等时选择、tracking 更新时机、非单调 Segment 轴行为。
10. RTOS Linux 上可用的文件系统、TLS、WebSocket/SSE、线程与 socket 能力；三方库引入前的交叉编译验证。

## 7. 结论

成熟 VNA 软件的核心不是一张图，而是“能力发现—扫频执行—不可变数据—误差校准—分析判定—双控制面—可追溯持久化”的完整闭环。`Diagram` 只是显示域的一部分；Marker、Limit、Memory、Calibration、Trigger、SCPI 状态、文件与诊断都必须作为一等能力进入总体设计。

本文可作为后续领域模型、驱动适配契约、数据处理图、Web API、SCPI 命令集和分阶段规格的功能检查表；它不替代这些更精确的设计文档。
