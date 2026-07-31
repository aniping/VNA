# 商用 VNA 外部行为基线

> 调研范围：Keysight PNA/ENA、Rohde & Schwarz ZNB/ZNBT、Copper Mountain Technologies（CMT）VNA 的官方帮助与官方手册，资料核对日期为 2026-07-17。

## 证据边界

本文只归纳厂商公开的用户对象、SCPI 行为和数据访问层级，用作本项目的外部行为参照。公开资料不能证明商用品内部采用了何种线程、队列、缓存、类或驱动 ABI；下文的项目模型是跨厂商行为的归一化设计，不是对闭源内部架构的猜测。不同厂商同名命令的数据位置也可能不同，不能仅凭 `RDATA`、`SDATA`、`FDATA` 等名称类推。本文把结论分为三类：多家官方资料共同支持的外部行为、单一厂商特有的兼容行为、仍待本项目确认的设计决策；后两类不会冒充行业标准。本轮未采用博客、论坛、第三方封装库或反向工程资料。

## 跨厂商共同外部关系

三家公开模型的稳定交集是：`Instrument` 管理多个 `Channel`；Channel 是刺激、扫频、触发、IFBW、平均及校准应用的主要作用域；每个 Channel 产生一个或多个 Measurement/Trace 结果；Marker 针对特定结果及其刺激轴进行读值或搜索；Window/Diagram 负责显示组织。Keysight 明确区分 Measurement 与 Window 中的显示 Trace，并保留采集时的刺激快照；R&S 则允许一个 Diagram 显示来自不同 Channel 的 Trace。[Keysight Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm)、[Keysight 对象编号说明](https://helpfiles.keysight.com/csg/N52xxA/Programming/Learning_about_GPIB/Referring_to_Traces_Measurements_Channels_Windows_Using_SCPI.htm)、[R&S ZNB/ZNBT User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf)

据此，本项目采用以下归一化关系；它是架构选择，不是三家共同公开的内部对象图：

```text
Instrument
├─ Channel ─ AnalysisTrace(TraceSourceSpec) ─ Marker/Limit
└─ Display ─ Window/Diagram ─ TracePlacement → AnalysisTrace
```

`AnalysisTrace` 是稳定的可分析对象；Live Source 内的 `MeasurementSpec` 只表达“测什么”，Math、Frozen/Memory Snapshot 和 Imported Data 使用其他 Source 变体。`TracePlacement` 表示 Diagram 内的显示关联和外观。该分离避免让图表对象拥有采集或判定状态，也避免为 R&S/CMT 并不存在的独立 Measurement 再造一整套浅 CRUD 生命周期。Channel、AnalysisTrace、Diagram、Placement 和 Marker 的标识必须带类型和作用域，不能压成同一种裸整数；内部历史 ID 不重新绑定，外部数字索引按方言规则处理。CMT 将测量、格式、数学和 Marker 暴露在 Channel/Trace 命令树中，说明兼容层需要把一个厂商式 Trace 命令原子映射到 AnalysisTrace 与 Placement 两侧。[CMT `CALCulate` 命令树](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html)

## 扫频、触发与完成同步

商用品普遍区分连续、保持和单次测量；Keysight 还公开 `GROups` 扫频。扫频启动属于可与后续命令重叠的操作，不能把“命令已接受”当成“结果已完成”。Keysight 明确警告：在扫频完成前执行 Marker 搜索可能得到错误结果，并提供 `*WAI`、`*OPC`、`*OPC?` 等同步方式；CMT 文档也用 `*OPC?` 等待 `TRIG:SING` 完成；R&S 规定 `*OPC?` 等待 pending operations，而 `*OPC` 在完成时置位事件寄存器。[Keysight 扫频模式](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Sense/Sweep_SCPI.htm)、[Keysight 命令同步](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)、[CMT `*OPC?`](https://coppermountaintech.com/help-cmtvna/Programming-Manual/opc_question.html)、[R&S Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html)

项目约束应是：每次逻辑扫频有独立 `sweep_id` 和明确的完成、取消、超时、触发未到、驱动失败终态；Web 可以接收点或分块预览，但任何正式操作只使用其命令要求的完整 typed result。Receiver/network 查询、校准采集/求解和 Touchstone 分别固定完整 A/B/标准件输入；缺少非 C 层 receiver/ratio/corrected/ProcessedNetwork 时，以 `MaterializeMeasurementStageOperation` 从 A/B 惰性生成不带 Trace/Marker/Limit revision 的 `MeasurementStageSnapshot`；Trace、Marker、Limit 与格式化导出才固定 C 层 AnalysisPublication。Frozen、Imported 或 Derived Trace 可以从不可变 typed inputs 直接产生 C，不要求伪造新 Sweep。每层完整结果独立原子发布，失败或取消的预览不得提升为正式结果。SCPI 默认读取“最近一次已完成结果”还是显式指定结果，需要固定为可测试规则；任何等待者都必须绑定具体 Operation/QueryTicket，不能等待一个会被下一次扫频覆盖的全局布尔状态。

## 数据处理层级与结果快照

CMT 公开区分 Raw Receivers、Raw S-parameter、校准标准件采集、Error Terms、Corrected Data 和 Formatted Data；Keysight 也区分未校准复数数据、应用误差项后的复数数据和显示格式数据。[CMT Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html)、[Keysight `CALCulate:DATA`](https://helpfiles.keysight.com/csg/e5080a/programming/gp-ib_command_finder/calculate/data.htm)

结合已确认的驱动边界，本项目数据流应保持类型分层：逐刺激点复数 `a/b` 波量进入由 Profile 验证的类型化 RF 图，图中以正交的 average `input_stage`、`sample_boundary` 和 `mode = FiniteBatch | SlidingWindow | Cumulative | VendorRunning` 区分 receiver-wave、measured-ratio、factory-corrected-ratio 和 user-corrected-network。SlidingWindow 使用预留固定 contribution ring 保存逐点复数 contribution/weight/quality，不依赖 A retention；VendorRunning 只有在 Compatibility Profile 冻结 update-kernel/state schema 后才能使用。Keysight 公开 ratio（ENA 可含端口/工厂特性修正）→sweep average→用户 Error Terms；CMT 内部数组资料公开 receiver-wave average→ratio→用户 correction，但需按目标型号/固件回归。之后才进入电延迟、时域/门控、迹线数学和 LogMag、Phase、Smith、SWR 等分析阶段。不得平均显示格式，不得隐式叠加底软与上层平均，也不得用一个可变数组在各阶段原地复用。正式 provenance 按层绑定：A 保存 `LogicalSweepId + BoardRunEvidence[]`，逐板绑定 Manifest、BoardRun generation、完成账本和实际刺激/路径/采集质量，默认单板时数组长度为 1；B 保存 `measurement_snapshot_id`、有界 `AverageContributionRef`、平均/修正与网络语义——finite 可列受 factor 上限约束的 Sweep IDs，sliding 保存有界窗口、固定 ring 的结构共享引用与 accumulator snapshot，cumulative/VendorRunning 保存 accumulator snapshot ID、generation/count、首末序号范围和滚动强摘要；C 保存 `analysis_publication_id`、Trace/pipeline/Marker/Limit revision 和 typed `AnalysisInputRefSet`。只有 Live C 反查 B；Frozen/Imported/Derived C 不强制携带 sweep 或当前 Channel CalSet。三层都保存软件/schema/Profile revision、完成时间和本层质量；修改当前 Channel 不得改写历史结果的含义。预览事件可以引用正在构建的扫频，但不能与正式快照共享可变所有权。

## 校准

公开行为支持的共同流程是：定义 Cal Kit/标准件 → 建立引导或非引导校准会话 → 按步骤采集标准件复数数据 → 求解 Error Terms → 保存 Correction Set → 应用于 Channel。Keysight Cal Set 可同时包含标准件原始测量和求得的误差项，并记录频率、功率、点数等条件；CMT 将标准件临时数组与求解后的误差项数组分开；R&S 公开 Cal Group/Calibration Pool 及 `Cal`、插值、关闭等状态。[Keysight 校准数据](https://helpfiles.keysight.com/csg/N52xxB/Programming/Learning_about_COM/Read_and_Write_Calibration_Data_using_COM.htm)、[CMT 校准数组](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html)、[R&S ZNB/ZNBT User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf)

因此校准不能建模成单一开关；校准数据集必须带方法、端口、刺激条件、套件与版本信息，并显式报告完全匹配、插值、外推、关闭或失效。求解完成的 Correction Set 宜作为不可变版本保存，Channel 只引用所应用版本；重新校准产生新版本，避免正在查询的历史结果被后台替换。校准后的 Verification/Confidence Check 还必须是独立 Plan/Operation/Result：测量独立已表征 artifact，与 characterization/uncertainty limit 比较，输出 Pass/Fail/Indeterminate，但不修改 Correction Set/Binding，也不冒充单个部件认证。Keysight [System Verification](https://helpfiles.keysight.com/csg/m9485a/support/system_verification.htm) 与 CMT [Confidence Check](https://coppermountaintech.com/help-cmtvna/1-port/confidence-check2.html) 支持这个外部边界。是否保留全部标准件原始采集、允许哪些插值/外推，以及具体误差模型和 verification limit，仍应按首版支持范围通过兼容/算法计量门禁确认。

## SCPI 队列与状态

Socket SCPI 需要保留仪器式顺序语义：命令按接收顺序解析，查询响应按顺序输出；若前一查询响应未读便收到冲突查询，应按选定兼容语义记录并报告错误，不能静默覆盖。错误进入有界 FIFO，`SYST:ERR?` 每次读取并弹出一条，空队列返回规范的 no-error 响应；队列满时的保留和溢出策略也必须固定。`*CLS`、`*ESR?`、`*STB?`、`*ESE`、`*SRE`、`*OPC`、`*OPC?` 的置位、清除和读取副作用必须可测试，并区分命令、执行、设备、查询等错误类别。Keysight、CMT 与 R&S 的官方资料均公开了错误队列、状态字节或事件寄存器的这类语义。[Keysight 命令同步](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)、[CMT `SYST:ERR?`](https://coppermountaintech.com/help-r/systerr_.html)、[R&S Instrument Error Checking](https://www.rohde-schwarz.com/in/driver-pages/remote-control/instrument-error-checking_231244.html)

Web 与 SCPI 应调用同一应用命令层，以免形成两套业务规则；这属于本项目架构约束，并非厂商内部实现结论。

## 尚待闭合的证据与 Profile

1. **兼容 Profile**：首个 SCPI 目标型号/固件、selection scope、对象编号、队列/状态/同步/删除副作用，以及 Segment/Average/Marker/Limit/处理顺序的厂商边界；推荐先做 Keysight PNA 公共子集并保留项目原生显式-ID Profile。
2. **Board Profile**：多 Channel 资源图、端口/source/receiver/route、trigger/abort、独立 RF SafetyLane/physical kill、`a/b` wave definition、quality、完整 forward/reverse bundle、Clock/Coherence Domain、timebase/epoch/skew、容量与并发；未知能力默认互斥、单板执行或拒绝，不合成跨板相干结果。
3. **算法/计量 Profile**：校准 solver、插值、独立 verification artifact/uncertainty limit、Marker/Limit 数值边界、port extension、renormalization、fixture、mixed-mode、time-domain/gate 的黄金数据、可溯源件和商用参考仪器对照。
4. **平台 Profile**：完整快照保留、RAM/Flash/吞吐、WebSocket/SSE、文件系统原子性、依赖版本、TLS、安全与长稳测试。
5. **少量产品范围**：跨 Sweep statistics、Equation Editor、advanced TDR/AFR/eye、报告、身份/SCPI 网络策略和 Pro/HW 授权组合；完整清单及推荐默认见 [逐项对齐矩阵](../design/feature-alignment-matrix.md)。普通 Limit/Ripple、明确类型的专用判定与跨 Sweep 生产资格策略的责任边界已经定案，不再作为一个含糊的“专业 Limit”产品项。
