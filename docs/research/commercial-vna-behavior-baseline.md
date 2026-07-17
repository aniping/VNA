# 商用 VNA 外部行为基线

> 调研范围：Keysight PNA/ENA、Rohde & Schwarz ZNB/ZNBT、Copper Mountain Technologies（CMT）VNA 的官方帮助与官方手册，资料核对日期为 2026-07-17。

## 证据边界

本文只归纳厂商公开的用户对象、SCPI 行为和数据访问层级，用作本项目的外部行为参照。公开资料不能证明商用品内部采用了何种线程、队列、缓存、类或驱动 ABI；下文的项目模型是跨厂商行为的归一化设计，不是对闭源内部架构的猜测。不同厂商同名命令的数据位置也可能不同，不能仅凭 `RDATA`、`SDATA`、`FDATA` 等名称类推。本文把结论分为三类：多家官方资料共同支持的外部行为、单一厂商特有的兼容行为、仍待本项目确认的设计决策；后两类不会冒充行业标准。本轮未采用博客、论坛、第三方封装库或反向工程资料。

## 跨厂商共同对象模型

三家公开模型的稳定交集是：`Instrument` 管理多个 `Channel`；Channel 是刺激、扫频、触发、IFBW、平均及校准应用的主要作用域；每个 Channel 产生一个或多个 Measurement/Trace 结果；Marker 针对特定结果及其刺激轴进行读值或搜索；Window/Diagram 负责显示组织。Keysight 明确区分 Measurement 与 Window 中的显示 Trace，并保留采集时的刺激快照；R&S 则允许一个 Diagram 显示来自不同 Channel 的 Trace。[Keysight Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm)、[Keysight 对象编号说明](https://helpfiles.keysight.com/csg/N52xxA/Programming/Learning_about_GPIB/Referring_to_Traces_Measurements_Channels_Windows_Using_SCPI.htm)、[R&S ZNB/ZNBT User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf)

据此，本项目宜使用以下归一化关系：

```text
Instrument
├─ Channel ─ MeasurementResult ─ Marker
└─ Display ─ Window/Diagram ─ TracePresentation → MeasurementResult
```

`MeasurementResult` 表示测量量及复数结果，`TracePresentation` 表示格式、缩放和可见性。该分离能避免让图表对象拥有采集状态，但是否作为首版硬性模型仍需决策。Channel、Measurement、Window、Trace 和 Marker 的标识应是带作用域的稳定标识，不能把各自编号压成同一种裸整数；删除显示迹线也不应顺带删除历史测量快照。CMT 将测量、格式、数学和 Marker 暴露在 Channel/Trace 命令树中，说明外部兼容层应允许把厂商式 Trace 路径映射到上述两个内部概念。[CMT `CALCulate` 命令树](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html)

## 扫频、触发与完成同步

商用品普遍区分连续、保持和单次测量；Keysight 还公开 `GROups` 扫频。扫频启动属于可与后续命令重叠的操作，不能把“命令已接受”当成“结果已完成”。Keysight 明确警告：在扫频完成前执行 Marker 搜索可能得到错误结果，并提供 `*WAI`、`*OPC`、`*OPC?` 等同步方式；CMT 文档也用 `*OPC?` 等待 `TRIG:SING` 完成；R&S 规定 `*OPC?` 等待 pending operations，而 `*OPC` 在完成时置位事件寄存器。[Keysight 扫频模式](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Sense/Sweep_SCPI.htm)、[Keysight 命令同步](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)、[CMT `*OPC?`](https://coppermountaintech.com/help-cmtvna/Programming-Manual/opc_question.html)、[R&S Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html)

项目约束应是：每次扫频有独立 `sweep_id` 和明确的完成、取消、超时、触发未到、驱动失败终态；Web 可以接收点或分块预览，但 Marker、保存、校准求解、SCPI 正式数据查询及完成同步只使用完整扫频快照。完整结果应在一次成功扫频结束时原子发布，失败或取消的预览不得提升为正式结果。SCPI 默认读取“最近一次已完成结果”还是显式指定结果，需要固定为可测试规则；任何等待者都必须绑定具体执行实例，不能等待一个会被下一次扫频覆盖的全局布尔状态。

## 数据处理层级与结果快照

CMT 公开区分 Raw Receivers、Raw S-parameter、校准标准件采集、Error Terms、Corrected Data 和 Formatted Data；Keysight 也区分未校准复数数据、应用误差项后的复数数据和显示格式数据。[CMT Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html)、[Keysight `CALCulate:DATA`](https://helpfiles.keysight.com/csg/e5080a/programming/gp-ib_command_finder/calculate/data.htm)

结合已确认的驱动边界，本项目数据流应保持类型分层：逐刺激点复数 `a/b` 波量 → 未校准测量量/比值 → 误差修正后的复数结果 → 电延迟、时域/门控、迹线数学等处理结果 → LogMag、Phase、Smith、SWR 等显示数据。不得用一个可变数组在各阶段原地复用。正式快照至少绑定 `sweep_id`、`channel_id`、配置修订号、实际刺激轴、端口/接收路径、完成状态、校准数据集 ID 和数据阶段，并保留点序、单位、无效点及采集诊断；修改当前 Channel 不得改写历史结果的含义。预览事件可以引用正在构建的扫频，但不能与正式快照共享可变所有权。

## 校准

公开行为支持的共同流程是：定义 Cal Kit/标准件 → 建立引导或非引导校准会话 → 按步骤采集标准件复数数据 → 求解 Error Terms → 保存 Correction Set → 应用于 Channel。Keysight Cal Set 可同时包含标准件原始测量和求得的误差项，并记录频率、功率、点数等条件；CMT 将标准件临时数组与求解后的误差项数组分开；R&S 公开 Cal Group/Calibration Pool 及 `Cal`、插值、关闭等状态。[Keysight 校准数据](https://helpfiles.keysight.com/csg/N52xxB/Programming/Learning_about_COM/Read_and_Write_Calibration_Data_using_COM.htm)、[CMT 校准数组](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html)、[R&S ZNB/ZNBT User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf)

因此校准不能建模成单一开关；校准数据集必须带方法、端口、刺激条件、套件与版本信息，并显式报告完全匹配、插值、外推、关闭或失效。求解完成的 Correction Set 宜作为不可变版本保存，Channel 只引用所应用版本；重新校准产生新版本，避免正在查询的历史结果被后台替换。是否保留全部标准件原始采集、允许哪些插值/外推，以及具体误差模型，仍应按首版支持的校准方法单独确认。

## SCPI 队列与状态

Socket SCPI 需要保留仪器式顺序语义：命令按接收顺序解析，查询响应按顺序输出；若前一查询响应未读便收到冲突查询，应按选定兼容语义记录并报告错误，不能静默覆盖。错误进入有界 FIFO，`SYST:ERR?` 每次读取并弹出一条，空队列返回规范的 no-error 响应；队列满时的保留和溢出策略也必须固定。`*CLS`、`*ESR?`、`*STB?`、`*ESE`、`*SRE`、`*OPC`、`*OPC?` 的置位、清除和读取副作用必须可测试，并区分命令、执行、设备、查询等错误类别。Keysight、CMT 与 R&S 的官方资料均公开了错误队列、状态字节或事件寄存器的这类语义。[Keysight 命令同步](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)、[CMT `SYST:ERR?`](https://coppermountaintech.com/help-r/systerr_.html)、[R&S Instrument Error Checking](https://www.rohde-schwarz.com/in/driver-pages/remote-control/instrument-error-checking_231244.html)

Web 与 SCPI 应调用同一应用命令层，以免形成两套业务规则；这属于本项目架构约束，并非厂商内部实现结论。

## 仍需明确的项目决策

1. 首版 SCPI 兼容目标：Keysight/R&S/CMT 中的一种方言，还是带厂商别名的公共子集。
2. 多 Channel 共享板卡资源时串行、并行或能力驱动的仲裁规则。
3. 首版支持 `SINGle`、`CONTinuous`、`GROups`、外部触发、点触发及平均完成中的哪些语义。
4. `MeasurementResult` 与 `TracePresentation` 是否正式分离，以及一份结果能否被多个图表呈现。
5. 校准方法清单、标准件模型、插值/外推边界、失效与告警规则。
6. Web 与多个 SCPI 会话并发写同一 Channel 时的控制权、冲突结果、审计和 selected Channel/Trace 上下文范围。
7. 完整快照的保留数量、内存上限、导出格式，以及预览流的限速与背压策略。
