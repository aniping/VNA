# 商用 VNA Sweep 与采集官方证据逐项对齐

本文只研究 Keysight PNA/ENA、Rohde & Schwarz ZNA/ZNB、Copper Mountain Technologies（CMT）公开的一手资料。主表逐项映射 [仪器、采集与校准对齐分册](../design/alignment/01-instrument-acquisition-calibration.md) 中的 `IAC-005` 至 `IAC-032`；第六节补齐 `IAC-001..004`、`IAC-033..038` 的跨文档证据投影，`IAC-039..046` 由校准/处理证据分册逐项覆盖。`IAC-021` 在本文只研究 Average 采样边界，不据此决定校准采集策略。

检索日期：2026-07-17。

## 结论

1. 三家产品共同支持以 Channel 为采集作用域的频率、功率、IFBW、Sweep、Trigger 和 Average 设置；Linear、Log、Segment、固定频率/时间、Power Sweep 都有官方依据。类型化 `StimulusDefinition`、`SweepPlan` 和统一 Web/SCPI Command Core 属于可靠的 E3 架构推导，不是凭空发明。
2. 三家都公开了“接收机复数数据 → 比值/测量量 → 校正/数学处理 → 格式化/显示”的分层数据链。raw、corrected、formatted 必须成为不同的数据阶段，不能只保存一份最终屏幕数组。
3. Average 的“存在、factor、clear、计数、在格式化前处理复数数据”属于 E1；但输入 stage 与采样边界并非跨厂商一致。Keysight ENA 的公开流程是复数比值、Port Characteristics Correction（端口/工厂特性修正）、Sweep Average、Raw Data，再应用用户 Error Correction；PNA 的公开访问模型也把 averaging 放在 raw measurement 与 Error Terms 之间。CMT 把 Raw Receiver Array 的逐点平均放在 Raw S-parameter ratio 和用户 correction 之前，但其用户页也以 S-parameter value 描述平均，因此必须按目标型号/固件回归。这些差异必须作为兼容策略，不能伪装成行业唯一算法。
4. 完整二端口结果需要两个源方向属于 E1；Keysight 明确说明标准扫描先 forward 后 reverse，CMT 官方应用资料也展示 Port 1 与 Port 2 两次激励。项目 E3 流程是 Compiler 把这些需求合并成 `SweepIntent + ConservativeResourceClaim`，Resource Arbiter 按 topology epoch 预准入，Board `prepare` 才产生不可变 `PreparedExecutionManifest`，精确预留/lease upgrade 后执行，并只发布一个原子 A 层 `CompletedSweepBundle`；公开手册不能证明公司底软的实际顺序、轴一致性和失败原子性。
5. 商用品允许扫描中显示当前点或进度，同时用 `*OPC?`、`*WAI` 或阻塞 trigger command 表示完成。由此可以确定“Preview 与完成必须区分”的外部需求；但 Preview 绝不晋升为正式快照，正式网络数据/Touchstone 固定 B 层 `CompletedMeasurementBundle`，Marker/Limit/Trace 导出固定相应 C 层 `AnalysisPublication`，这是本项目的 E3 一致性策略。
6. 任何厂商手册都不能证明公司单板会交付什么形态的 `a/b`、波量定义、质量位、chunk 生命周期、背压、abort SLA 或完整逻辑扫描边界。这些只能由公司底软接口说明、CapabilityDescriptor、Mock/Replay 契约和目标板实测形成 E4 证据。

## 证据等级与判定纪律

- **E1 — 跨厂商官方共性**：至少两家厂商的一手资料明确支持同一外部行为。可以作为产品核心行为基线。
- **E2 — 单厂商行为或厂商差异**：官方资料明确存在，但并非跨厂商一致。只能作为默认选择或 Compatibility Profile。
- **E3 — 架构推导**：为稳定实现 E1/E2 而引入的 revision、Manifest、typed ID、快照、队列、原子发布和适配 seam。不得描述成厂商内部实现事实。
- **E4 — 公司硬件/平台事实**：端口拓扑、频率/功率/IFBW 档位、`a/b` 语义、trigger I/O、chunk/abort、质量位和目标 RTOS 行为。必须来自公司底软资料或实测。

以下表格中的“未公开”表示本轮一手资料没有给出足以支撑该断言的证据，不表示商用品内部一定没有该机制。

## 官方资料索引

### Keysight PNA / ENA

- **K1** — [PNA N52xxB Sweep Settings](https://helpfiles.keysight.com/csg/N52xxB/S1_Settings/Sweep.htm)：Linear/Log/Power/CW Time/Segment、dwell/delay、实际 sweep time、标准/逐点扫描、forward/reverse 次序和 sweep indicator。
- **K2** — [ENA E5080B Sweep Settings](https://helpfiles.keysight.com/csg/e5080b/S1_Settings/Sweep.htm)：ENA 上相同的 Sweep 类型、Segment 参数、Power Sweep 和 timing 行为。
- **K3** — [PNA Trigger](https://helpfiles.keysight.com/csg/N52xxA/S1_Settings/Trigger.htm)：Internal/Manual/External、Global/Channel、Continuous/Groups/Single/Hold、Channel/Point trigger。
- **K4** — [ENA E5061B Trigger Setup](https://helpfiles.keysight.com/csg/e5061b/measurement/making_measurements/setting_up_trigger.htm)：触发源、逐 sweep/point 触发，以及只扫描更新已显示参数所需的刺激端口。
- **K5** — [PNA Accessing Data](https://helpfiles.keysight.com/csg/N52xxB/Programming/Accessing_Data_Descriptions.htm)：receiver、ratio、averaging、raw、corrected、trace math、formatter 和 displayed result 数据访问点。
- **K6** — [ENA E5061B Data Processing](https://helpfiles.keysight.com/csg/e5061b/product_information/general_principles_of_operation/data_processing.htm)：ADC/DFT、复数比值、Sweep Average、raw、corrected、formatted 的公开处理顺序。
- **K7** — [PNA Measurement Parameters](https://helpfiles.keysight.com/csg/N52xxB/S1_Settings/measurement_parameters.htm)：S 参数、ratioed/unratioed receiver、source port 和 forward/reverse 定义。
- **K8** — [PNA Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm)：Measurement 的数据处理职责，以及它保存“最后一次采集时”的 stimulus snapshot。
- **K9** — [PNA Remotely Specifying a Source Port](https://helpfiles.keysight.com/csg/N52xxB/Programming/Remotely_Specifying_a_Source_Port.htm)：逻辑端口、物理端口映射、字符串 source name 和动态端口号。
- **K10** — [PNA Channel Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Channel_Object.htm)：Channel 的 frequency、power、IFBW、sweep、trigger、source-port mode 和 timing 属性。
- **K11** — [ENA E5061B Averaging with a Single Trigger](https://helpfiles.keysight.com/csg/e5061b/measurement/making_measurements/making_averaging_measurement_with_single_trigger.htm)：Average trigger、factor、point-trigger 优先级及所需 trigger 数。
- **K12** — [PNA Command Synchronization](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)：overlapped measurement 与 `*OPC?`/`*WAI` 完成同步。

### Rohde & Schwarz ZNA / ZNB

本基线冻结参考 ZNA v39 与 ZNB/ZNBT v72；截至研究日，官方页面已发布 ZNA v40（2026-03-27）和 ZNB/ZNBT v73（2026-04-14）。升级参考版本必须重新核对章节、命令与回归行为，不能把 v39/v72 称为当前最新版。以下页码均指 PDF 页脚页码。

- **R1** — [R&S ZNA User Manual, version 39](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf)：§4.1.4 “Sweep control” p.115-119 覆盖 Lin/Log/Segmented/Power/CW/Time 与 range；§4.1.7 “Data flow” p.125-128 覆盖 Channel/Trace 阶段；§5.9.3 “Average tab” p.559；§5.10.4 “Trigger tab” p.581。
- **R2** — [R&S ZNB/ZNBT User Manual, version 72](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf)：Sweep 类型、start/stop/center/span、stepped/swept mode、source/receive ports 和 power；§4.1.5 data flow p.99-102。
- **R3** — 同一 [ZNB/ZNBT v72](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf) §4.1.5 p.99-102：Channel data flow 与 Trace data flow 图，明确 Average、unformatted、trace math、format 和 displayed quantity 的阶段关系；旧 v71 URL 已移除，不再作为证据。
- **R4** — [Hints and Tricks for Remote Control of Spectrum and Network Analyzers](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ef62/1EF62_1E.pdf) §3.2 p.6：连续扫描可能读到来自不同 sweep 的不一致数据，`INIT;*OPC?` 在 continuous 下可立即返回；远程确定性采集应使用 single sweep 与完成同步。
- **R5** — [R&S ZNBT Product Page](https://www.rohde-schwarz.com/us/products/test-and-measurement/vnas/rs-znbt-vector-network-analyzer_63493-58917.html)：多端口硬件同步采集和并行处理是 ZNBT 的具体实现能力，不是所有板卡的通用事实。

### Copper Mountain Technologies

- **C1** — [CMT Sweep Type](https://coppermountaintech.com/help-cmtvna/1-port/sweep-type.html)：Linear、Logarithmic、Segment、Power 和 CW Mode。
- **C2** — [CMT Segment Table Editing](https://coppermountaintech.com/help-cmtvna/1-port/segment-table-editing.html)：segment start/stop/points 及可选 IFBW、power、delay。
- **C3** — [CMT Trigger State Diagram](https://coppermountaintech.com/help-cmtvna/1-port/trigger-state-diagram.html)：Analyzer/Channel 两级状态、Hold/Initiated/Measurement、整扫/逐点 trigger 和 Average repeat。
- **C4** — [CMT Trigger Source](https://coppermountaintech.com/help-cmtvna/Programming-Manual/trigsour.html)、[Trigger Status](https://coppermountaintech.com/help-cmtvna/Programming-Manual/trigstat_.html) 与 [`*OPC?`](https://coppermountaintech.com/help-cmtvna/Programming-Manual/opc_question.html)：INT/EXT/MAN/BUS、HOLD/MEAS/WAIT 和完成等待。
- **C5** — [CMT Averaging Setting](https://coppermountaintech.com/help-cmtvna/1-port/averaging-setting.html)：逐点跨 sweep 平均、factor、当前计数和稳定完成条件。
- **C6** — [CMT Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html)：Raw Receiver、Raw S-parameter、Corrected Receiver/S-parameter、Corrected Data、Formatted Data 和 Stimulus Data 的公开顺序。
- **C7** — [CMT Measurement Parameter](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcpardef.html)：S11/S21/S12/S22、test receiver A/B、reference receiver R1/R2 和 stimulus port。
- **C8** — [CMT Diagram](https://coppermountaintech.com/help-cmtvna/1-port/diagram.html)：慢 sweep 的 current stimulus position indicator。
- **C9** — [CMT External Trigger Event](https://coppermountaintech.com/help-cmtvna/1-port/external-trigger-event.html)：一次 trigger 覆盖完整 measurement cycle 或一个 point，以及 point trigger 与 average 的消费规则。
- **C10** — [CMT Voltage and Current Measurements with a VNA and DMM](https://coppermountaintech.com/wp-content/uploads/2022/03/Voltage-and-Current-Measurements-with-a-VNA-and-DMM.pdf)：官方应用中 Port 1 forward 与 Port 2 reverse sweep 组成完整 S 参数采集。
- **C11** — [CMT RF Output](https://coppermountaintech.com/help-cmtvna/Programming-Manual/outp.html)：RF output on/off 及 RF off 时不可测量。
- **C12** — [CMT Frequency Data Query](https://coppermountaintech.com/help-cmtvna/Programming-Manual/sensfreqdata_.html)：读取每个实际 measurement point 的 frequency array。
- **C13** — [CMT S2VNA Operating and Programming Manual](https://coppermountaintech.com/wp-content/uploads/2025/04/S2VNA-Operating-and-Programming-Manual.pdf)：二端口产品的 CW time、Sweep、Trigger、Average 和 S 参数行为。
- **C14** — [CMT IF Bandwidth Setting](https://coppermountaintech.com/help-cmtvna/1-port/if-bandwidth-setting3.html)：IFBW 离散值、Channel 作用域与 measurement-time 影响。
- **C15** — [CMT Channel and Trace Setting](https://coppermountaintech.com/help-cmtvna/1-port/channel-and-trace-setting.html)：Channel 采集设置与多 Trace 分析设置的归属。
- **C16** — [CMT Measurement Capabilities](https://coppermountaintech.com/help-cmtvna/1-port/measurement-capabilities-1-por.html)：逻辑 Channel、多 Trace、Sij、Sweep 与 trigger 的型号能力边界。
- **C17** — [CMT SENSe Command Index](https://coppermountaintech.com/help-cmtvna/Programming-Manual/sense.html)：Average state/count/clear、IFBW、Sweep 和 receiver 相关命令入口。
- **C18** — [CMT ABORt](https://coppermountaintech.com/help-cmtvna/Programming-Manual/abor.html)：abort 当前 sweep 后 Single/Continuous Channel 的触发状态转换。
- **C19** — [CMT 1-Port Series Operating Manual](https://coppermountaintech.com/wp-content/uploads/2025/11/1-Port-Series-CMT-VNA-Operating-Manual.pdf)：Channel status、Cycle Time、Sweep/Trigger/Average 的当前产品手册。

## 一、配置、Stimulus 与 Sweep：IAC-005 至 IAC-015

| ID | 逐项对齐结论 | Keysight 官方证据 | R&S 官方证据 | CMT 官方证据 | E1/E2/E3/E4 判定 | 架构处置与未决边界 |
|---|---|---|---|---|---|---|
| IAC-005 | 正式结果必须绑定完成采集时使用的配置；运行中改参不能让一份结果混用两套设置。 | K8 明确 Measurement 保存最后采集时的 stimulus snapshot；采集后再改 Channel，Channel 当前值已不能准确描述旧数据。 | R4 明确连续模式读取结果可能来自不同 sweep，single + completion synchronization 才可靠。 | C3 明确设置变化会 abort 当前 measurement cycle 并回到 Stop，这是 CMT 的具体策略。 | **E1**：结果与一次完成采集的设置绑定。**E2**：CMT 采用改参即 abort；Keysight 暴露 last-acquired snapshot。**E3**：内部单调 revision、当前轮冻结旧 revision、下一轮原子采用；ADR-0014 决定 Web/SCPI 不暴露该 revision。**E4**：底软哪些字段可热更新。 | `ChannelConfigRevision` 在 `prepare` 时冻结进 Manifest；Preview 与 Completed 在内部携带 revision，协议只返回稳定 Operation/Snapshot 身份和业务状态。项目原生语义已定为普通改参下一完整轮原子生效；要求“改参即 abort”的方言由 Compatibility Profile 组合 restart，RF 安全字段则由底软能力契约声明。 |
| IAC-006 | start/stop 与 center/span 是等价编辑视图；正式数据必须携带实际 stimulus axis，客户端不得仅凭公式重建。 | K1/K2 提供 start/stop、center/span；K8 的 last-acquired stimulus snapshot 与 X-axis query 说明结果轴属于测量结果。 | R1/R2 的 stimulus 表明确 Lin Freq 的 start/stop/center/span。 | C2 的 segment 也支持 start/stop 或 center/span；C12 直接返回逐点频率数组。 | **E1**：两种坐标编辑和结果轴查询。**E3**：同时保存 user intent 与 normalized coordinates。**E4**：单板频率量化和实际 readback。 | 核心只保存一种规范化区间并由同一换算器派生其他字段；`CompletedSweepBundle.actual_axis` 是权威。频率范围、端点量化、重复点和反向轴能力由 Capability/prepare 决定。 |
| IAC-007 | RF output state 与目标 power 是采集设置；范围、离散档位和未稳幅/失锁不能由上层猜测。 | K10 暴露 SourcePortMode、TestPortPower、按端口 power 和 power-sweep start/stop。 | R1/R2 把 channel base power 定义为 test port 的 source power，并允许按 sweep/port 配置。 | C11 明确 RF on/off，RF off 时不能测量；C1/C2 支持 channel/segment power。 | **E1**：RF/power 可配置且受测量状态约束。**E2**：端口独立性和 RF-off 行为细节不同。**E3**：统一 SourceState 和质量原因。**E4**：范围、量化、level/lock 标志。 | `prepare` 返回 accepted/actual power、RF state 与警告；执行期 source unlock/unleveled 进入 quality/diagnostic。底软必须提供功率范围、耦合规则、稳幅/锁定可观测性。 |
| IAC-008 | 测量参数必须显式绑定 stimulus/source port 与 receive port；逻辑端口、物理端口、source 名称和 mixed-mode 不是同一概念。 | K9 明确 multiport test set 的 logical-to-physical mapping、字符串 source name 和动态数字端口。 | R1/R2 的 wave notation 包含 receive port 与 source port；R2 还允许特定模式下按端口设置 frequency/power。 | C7 的 A/B/R 查询以 stimulus port `n` 区分，Sij 也显式包含 source/receive 方向。 | **E1**：source/receive port 是测量语义的一部分。**E2**：Keysight 的动态编号/字符串名及各家命名差异。**E3**：稳定 typed ID 与独立 RouteResolver。**E4**：公司板卡物理拓扑和路由互斥。 | 定义 `PhysicalPortId`、`LogicalPortId`、`SourceId`、`ReceiverPathId`；外部方言编号只在协议适配层映射。Capability 必须给出可激励/可接收关系、路由和互斥。 |
| IAC-009 | IFBW 是 Channel 级基础采集条件；Segment override 是常见能力，但允许值和量化为型号事实。 | K1/K2 允许 channel IFBW 与逐 segment IFBW；sweep timing 随之变化。 | R1/R2 把 IFBW 放在 Channel/sweep 设置中，并允许 segmented 条件。 | C2 允许逐 segment IFBW；C14 给出离散列表并说明窄 IFBW 增加测量时间。 | **E1**：Channel IFBW 与常见 Segment override。**E2**：离散值表和超范围处理不同。**E3**：requested/actual 分离。**E4**：公司板卡档位与 override 能力。 | `IfBandwidth` 使用 Hz 强类型；Capability 返回合法区间或离散集合；Manifest 记录每段实际 IFBW，不让 Web/SCPI 分别量化。 |
| IAC-010 | Linear 与 Log 是独立的频率 Sweep 类型；Log 轴不能由前端用 Linear 点数公式伪造。 | K1/K2 明确 Linear 与 logarithmic stepping/display。 | R1/R2 明确 Lin Freq 与 Log Freq。 | C1 明确 Linear 与 Logarithmic。 | **E1**：三家一致。**E3**：`LinearSweepPlan`/`LogSweepPlan` 类型和轴生成/回读契约。**E4**：频率分辨率、最大点数、反向能力。 | 两种计划独立校验；Log 要求正频率；处理链只消费 Manifest/Completed 中实际轴。是否接受单点、反向 Log、量化重复点由 Capability 决定。 |
| IAC-011 | Segment 是一次逻辑 Sweep 内的多段定义，常见字段为 range/points/IFBW/power/delay 或 time；段限制存在厂商差异。 | K1/K2 支持每段 power、IFBW、sweep time，并把各段组合成单条 trace；普通模式要求单调不重叠，特定 arbitrary 模式可反向/重叠。 | R1/R2 支持 Segmented sweep 和每段 stimulus 条件。 | C2 要求 start/stop/points，可选 IFBW/power/delay，并规定相邻段不重叠。 | **E1**：Segment 表和逐段 override。**E2**：重叠、反向、排序、字段及点数限制不同。**E3**：版本化整表、原子 apply、保存显式 segment boundaries。**E4**：底软支持字段。 | 首版 Profile 应冻结“单调、不重叠、显式拼接轴”的最小公共子集；若兼容 Keysight arbitrary segment，必须单独开 capability/profile，不能悄然放宽核心规则。 |
| IAC-012 | 固定频率的单点/重复采样，与横轴为时间的 CW-Time/Time sweep，是不同轴域；也不同于频域数据的时域变换。 | K1/K2 的 CW Time 明确固定频率、横轴为 time、配置 sweep time 和 points。 | R1/R2 分开列出 CW Mode 和 Time；固定 frequency 下一个以 points、一个以 stop time 描述。 | C1 提供 CW Mode；C13 明确 CW time 以固定频率显示 data versus time。 | **E1**：固定频率与 time-axis 模式存在且轴语义独立。**E2**：名称和 CW Mode/Time 的划分不同。**E3**：`CwPoint`、`CwTimeSweepPlan`、`AxisDomain::Time`。**E4**：持续采样、时间戳和抖动能力。 | UI/SCPI/文件都返回轴域与单位；不能把 CW-Time 叫作 VNA Time Domain。无真实时间戳的板卡只可暴露 sample-index 轴，不能伪造等间隔绝对时间。 |
| IAC-013 | Power Sweep 是固定频率下的功率轴扫描，是否可执行取决于 source 可编程范围、attenuator/settling 等硬件条件。 | K1/K2 明确 start power、stop power、CW frequency、points；PNA 对 attenuator switching 有限制，并建议 dwell 以改善准确度。 | R1/R2 明确 Power sweep、start/stop power 和 CW frequency。 | C1 明确 Power sweep；CMT 多型号手册列出固定频率下的 linear power sweep。 | **E1**：三家一致支持概念。**E2**：range、方向、attenuator、ranging 行为不同。**E3**：capability-gated `PowerSweepPlan`。**E4**：公司 source 的 range/step/settling/level status。 | Core 保留完整类型，但只有 Capability 明确支持时开放；Manifest 使用实际功率轴。这不是新增产品决策：首板能否交付真实执行仅由 source range/step/settling/level 等 E4 证据关闭，能力缺失时 Web/SCPI 一致返回 unsupported。 |
| IAC-014 | Hold、Single、Continuous 是跨厂商共同模式；指定 N 次后 Hold 的 Groups 是 Keysight 的明确方言能力，不是三家统一名称。 | K3 明确 Continuous/Groups/Single/Hold，Groups count 完成后进入 Hold。 | R4 与 R1 采用 Continuous/Single，并建议远程控制用 Single。 | C3 明确 Hold/Single/Continuous 的 Channel 状态转换。 | **E1**：Hold/Single/Continuous。**E2**：Keysight Groups 及 count 语义。**E3**：每轮独立 `SweepOperation` 与 B 层 `CompletedMeasurementBundle`、Groups completion fence。 | 核心可提供 `Hold/Single/Continuous/FiniteCount`，SCPI Profile 再把 `FiniteCount` 映射为 Groups。`start accepted`、每轮 complete、finite sequence complete 和 abort terminal 必须是不同事件。 |
| IAC-015 | 请求参数与实际采集参数必须分开；实际 X 轴和完成 timing 要与数据一起可追溯。 | K1 说明手输 sweep time 可能被向上量化，actual sweep time 还包含 overhead；K8 说明测量保存 last-acquired stimulus，且可返回 X-axis values。 | R1/R2 公开 sweep time、points、stepped/swept 和按端口条件，但未公开本项目所需的统一 Manifest。 | C12 返回逐点频率数组；C19 的 Channel status 还显示 measured cycle time。 | **E1**：结果轴/参数可读回，timing 是采集条件。**E2**：actual-time 的定义和可查询粒度不同。**E3**：不可变 PreparedExecutionManifest 与组合 A provenance。**E4**：底软实际轴、actual power/IFBW/delay/time 回报。 | Compiler 只形成 `SweepIntent + ConservativeResourceClaim`；Arbiter 按 topology epoch 保守预准入后，每块参与板的 `prepare` 再生成 requested→actual 映射、轴、segment boundary、source/route、精确 resource/memory bounds 和 estimated/actual timing；随后按全组 Manifest 做校准/能力验证、精确预留并把预准入 token 升级为各板 `ExecutionLease`，成功后才 start。Completed A 以 `BoardRunEvidence[]` 引用 parent Manifest set，默认单板时长度为 1；没有底软 readback 的字段必须标 `requested_only`，不能冒充 actual。 |

## 二、Trigger 与 Average：IAC-016 至 IAC-021

| ID | 逐项对齐结论 | Keysight 官方证据 | R&S 官方证据 | CMT 官方证据 | E1/E2/E3/E4 判定 | 架构处置与未决边界 |
|---|---|---|---|---|---|---|
| IAC-016 | Internal/Immediate、Manual/Bus 和 External 是成熟触发来源；External 只在真实 trigger input 存在时有效。 | K3 提供 Internal、Manual、External；ENA K4 还包含 bus/program trigger。 | R1 证明 ZNA 有独立 trigger 子系统/Trigger tab，但本轮未逐项核实其完整 source 枚举，不能把 bus event、`*TRG` 与 trigger source 混写。 | C4 明确 INT/EXT/MAN/BUS。 | **E1**：Keysight+CMT 两家完整覆盖 internal/manual/bus/external。**E2**：R&S/各家名称、global source 数量和 connector 行为需目标命令树核实。**E3**：统一 `TriggerSource`。**E4**：外部引脚、电平、极性、延迟、trigger out。 | Web 的“立即触发”与 SCPI Bus Trigger 产生同一领域事件；Web 不模拟硬件边沿。Capability 未声明 External 时，UI 隐藏且 SCPI 返回 unsupported。 |
| IAC-017 | Trigger 既有作用域，又有消费粒度，并需要可观察的等待/测量状态；厂商状态名和支持粒度不同。 | K3 区分 Global/Channel 与 Channel/Point；相关状态由 trigger model 暴露。 | R1 公开 channel/trigger/sweep 控制，但不同 ZNA/ZNB 选件的粒度不是统一硬件事实。 | C3 明确 Analyzer/Channel 两级状态；C4 可查询 HOLD/WAIT/MEAS；C9 区分 whole sweep 与 point。 | **E1**：global/analyzer 与 channel scope、whole-sweep/point、waiting/measuring 可观察。**E2**：segment/trace/point 名称和支持组合不同。**E3**：`Armed/WaitingTrigger/Acquiring` phase。**E4**：板卡能否逐 point/segment 接受边沿。 | Trigger Profile 声明 scope × granularity 矩阵；Operation phase 是 Web 和 SCPI 的唯一状态源。等待触发期间的 timeout/cancel 不由协议线程私自处理。 |
| IAC-018 | “一次 trigger 消费到哪里”必须显式：整次 measurement cycle、一个 Channel sweep、一个 point 或 Average 的 N 次重复。公开资料没有给出公司板卡每个 source-state/sub-sweep 的规则。 | K3 说明 Channel/Point trigger，Groups/Single 的计数要等 Channel 所有 points/traces 完成后才递减；K11 说明 point trigger 对 Average 的优先级。 | R1 能配置触发与 sweep，但不能据此推断公司 forward/reverse sub-sweep 的 trigger consumption。 | C9 明确一个 trigger 可覆盖所有参与 Channel 的完整 measurement cycle，或仅一个 point；N×P average trigger 数也明示。 | **E1**：trigger consumption 与 scope/granularity/average 相关，必须定义。**E2**：各家计数边界不同。**E3**：Manifest 中冻结 `TriggerConsumptionPolicy` 与 expected count。**E4**：底软 forward/reverse 是否内部续扫。 | Profile 至少表示 `PerLogicalSweep`、`PerSourceState`、`PerPoint`；实际收到/消耗数进入 Operation diagnostics。未获 E4 前不能承诺一次外触发完成全二端口。 |
| IAC-019 | 跨 sweep 的 Average 必须在复数、未格式化数据层进行；精确输入 stage 与采样边界存在厂商差异，不能把屏幕 dB/phase 做算术平均。 | K6 的 ENA 流程为 complex ratio → Port Characteristics Correction → Sweep Average → Raw Data → user Error Correction；K5 的 PNA 访问模型也把 Average 放在 raw measurement 与 Error Terms 之间。 | R1 §4.1.7 p.125-128 / R3 §4.1.5 p.99-102 的 Channel data flow 把跨-sweep/channel Average 放在 correction/network processing 之后、per-trace format/display 之前。ZNA p.167/ZNB p.138 的 `AVG detector` 是**单个测量点 detector-time 内平均**，不是此处跨-sweep Average，必须分成另一配置/状态类型。 | C6 说明 Raw Receiver arrays 先逐点跨 N sweeps 平均，再由两个 receiver signals 形成 Raw S-parameter；C5 的用户描述以 S-parameter point 为平均对象，因此内部数组证据成立但仍需目标型号/固件回归。 | **E1**：Keysight+CMT 支持复数、逐点、格式化前跨-sweep 平均。**E2**：Keysight 是 ratio/可选工厂端口修正后 average、用户 Error Terms 前；CMT 内部数组是 receiver average 后再 ratio；R&S data flow 显示其 Channel Average 位于 correction/network processing 后。**E3**：`AveragePolicy` 正交声明 input stage、sample boundary、factory/user correction 边界与算法版本，并与 detector-time average 分型。**E4**：底软是否已内部平均及能否关闭。 | 项目原生推荐 `MeasuredRatio + LogicalSweep`；ENA Profile 可显式用 `FactoryCorrectedRatio`；CMT 内部数组 Profile 用 `ReceiverWaves + LogicalSweep/Point` 并做目标固件回归。R&S Profile 的 `CorrectedNetwork` 只在目标资料/黄金回归关闭后选择。Point/Sweep 是 sample boundary，不与数据 stage 混成一个枚举。严禁上层和底软重复平均。 |
| IAC-020 | Average 至少需要 state、factor、clear/restart、current count 和 complete；单个 sweep 完成不等于 average complete。 | K5/K6/K11 覆盖启停、指定次数、clear 后重计和 average-trigger completion。 | R1 §5.9.3 p.559 与 R2 的 Average tab 只证明存在 Channel Average 配置；本轮未核实 current count/complete 的 R&S 精确字段。 | C5 显示 factor、当前 iteration 和 `current == factor` 的稳定条件；C17 列出 state/count/clear。 | **E1**：Keysight+CMT 支持状态、factor、重启、进度/完成。**E2**：R&S 精确字段及达到 factor 后 running/sliding 或固定结果的行为需目标 Profile。**E3**：generation、per-point valid count、两个 completion event，以及不会随 running 时长增长的有界 provenance；每贡献 B、live C 合并和领域原子提交属于项目一致性设计。 | `AverageAccumulatorSnapshot` 包含 input_stage/sample_boundary/mode/factor/generation/accepted_count/complete；clear 原子换 generation。每个被接受贡献发布 B，并把 B、accumulator snapshot、`ChannelAverageHead`、`ChannelMeasurementHead` 在同一个 `DomainCommitBundle` 提交；Continuous/Average 过载下普通 live C 可 latest-wins 合并，对指定 completed B/Stage 的精确查询则启动不受 Live 合并影响的求值，相同 exact key 仍可 single-flight。B 层 `AverageContributionRef` 对 finite 保存受 factor 上限约束的 Sweep IDs，对 sliding 保存有界窗口 IDs + accumulator snapshot，对 cumulative/running 保存 accumulator snapshot ID、generation/count、首末序号范围和滚动强摘要。Web/SCPI 不得用 UI 本地计数，持续运行也不得让 metadata 无界增长。 |
| IAC-021 | Average 的一个 sample 边界必须与逻辑采集边界一致；Point Average、Sweep Average、一次 trigger 完成 N sweeps 是不同策略。校准采集策略本轮不定案。 | K11 明确 average-trigger 一次触发执行 factor 次 sweep，而 point trigger 优先并需要 points×factor 次触发；PNA 另有 sweep/point average。 | R1/R3 能证明 Channel Average 的处理位置，但不能证明本项目 full-port sample 的原子边界。 | C3/C9 给出 average repeat 和 point-trigger 优先规则。 | **E1**：Average sample/trigger 边界是可配置语义。**E2**：point/sweep/triggered-average 组合差异。**E3**：项目原生把一个完整 `CompletedSweepBundle` 定义为 LogicalSweep sample。**E4**：底软内部 averaging 与 F/R 执行顺序。 | 默认上层 sample 为完整 logical sweep bundle，只有 bundle complete 才计数；`F,R,F,R` 与 `F×N,R×N` 必须是不同 Board/Compatibility policy。校准相关部分留给校准证据分册。 |

## 三、Measurement 与数据阶段：IAC-022 至 IAC-028

| ID | 逐项对齐结论 | Keysight 官方证据 | R&S 官方证据 | CMT 官方证据 | E1/E2/E3/E4 判定 | 架构处置与未决边界 |
|---|---|---|---|---|---|---|
| IAC-022 | Channel 采集可以服务多个 measured quantity/trace；“测什么”与“如何格式化/显示”必须分开。厂商对象身份并不统一。 | K8 有独立 Measurement Object，定义 parameter 并驱动 raw→format processing；一个 Channel 可有多个 Measurement。 | R1/R3 的 Trace 选择 measured quantity，同时另有 Trace format；Channel data flow 再分给多个 Trace。 | C15 允许每 Channel 多 Trace；C7 的 measured parameter 与 format 分开设置。 | **E1**：一个 Channel 多 measured quantity/Trace，quantity 与 format 分离。**E2**：只有 Keysight 明确暴露独立 Measurement 身份。**E3**：统一 `AnalysisTrace` + 内嵌 `MeasurementSpec`。 | 不照抄任一厂商对象树。`MeasurementSpec` 是稳定类型化值；`AnalysisTrace` 承载分析 revision；Display placement 另建引用。无 Placement 的 Trace 仍可查询。 |
| IAC-023 | `S(out,in)` 的两个端口分别是接收端和激励端；二端口不能写死成四个枚举，更高端口数取决于硬件。 | K7 定义 S11/S21/S12/S22 和 forward/reverse，PNA 自动切换 source/receiver。 | R1/R2 以矩阵和 source-port wave notation 表示多端口参数；R5 是多端口具体产品例。 | C7 支持四个二端口 S 参数并绑定 stimulus port；C16 记录多端口产品的 Sij 能力。 | **E1**：参数化 Sij 与 source/receive direction。**E2**：最大端口数与自动切换方式不同。**E3**：typed port ID 的 `SParameter(out,in)` 与 Sweep Compiler。**E4**：可激励/接收端口集合。 | 核心算法按 N-port 编写；Capability 决定合法 Sij。新增 Trace 时重新编译所需 source states，不能在协议层把 S12/S22 当特殊命令分支。 |
| IAC-024 | Ratioed receiver、unratioed/absolute receiver wave 与 corrected S-parameter 是不同测量类型和数据阶段。 | K7 明确 S11 是 A/R1，支持任意 receiver ratio 与单 receiver absolute power；K5 区分 raw receiver/ratio、corrected 和 formatted result。 | R1/R2 明确 wave quantity 与任意 ratio，并说明它们与 system-error-corrected S/Z/Y 参数不同。 | C7 暴露 A/B、R1/R2、Sij；C6 区分 Raw/Corrected Receiver 和 Raw/Corrected S-parameter。 | **E1**：三类测量及阶段分离。**E2**：命名、绝对量单位和可否校正不同。**E3**：`ReceiverWave`/`ReceiverRatio`/`SParameter` 与 `MeasurementDataStage`。**E4**：公司 a/b 的物理单位与可溯源增益。 | API 查询必须同时指定 measurement identity 与 stage；类型系统禁止把 raw receiver buffer 标成 corrected S。分母接近零产生 invalid quality，而不是零或无限值静默穿透。 |
| IAC-025 | 多个 Trace 共享 Channel 采集是共性；只激励满足当前测量需求的端口是 Keysight ENA 的公开优化。具体“最小采集计划”仍是本项目架构。 | K4 明确每 Channel 只扫描更新 displayed trace parameters 所需的 stimulus ports；K1 的标准/point sequence 复用同一源方向的参数。 | R3 的一份 Channel data flow 扇出到多个 Trace。 | C15/C6 显示一份 Channel data 供多 Trace，但未公开需求合并算法。 | **E1**：Channel acquisition feeds multiple traces。**E2**：ENA 明确按所需 stimulus ports 最小化。**E3**：Sweep Compiler 合并全部 Live MeasurementSpec。**E4**：route 切换成本和可同时接收向量。 | 编译器输入为 Channel 当前所有 live consumers、导出/诊断所需量，输出去重 source states/receiver vectors 的 `SweepIntent + ConservativeResourceClaim`；新增/删除 Trace 只产生下一 revision 的 intent。最终 Manifest 只能由预准入后的 Board `prepare` 产生，不按 Trace 独立启动底软扫描。 |
| IAC-026 | 商用品确实存在 raw receiver/receiver-wave 数据层；这不能证明公司底软会按本项目接口交付逐点 `a/b`。 | K5/K7 证明 receiver A/B/R、raw receiver 与 ratio 数据存在。 | R1/R3 证明 ai/bi wave quantity 和 channel data flow。 | C6/C7 证明 Raw Receiver arrays 及 A/B/R 参数。 | **E1**：receiver-wave 数据层是商用 VNA 共性。**E3**：Board Adapter 是唯一 seam，统一 Real/Mock/Replay。**E4**：公司底软是否交付何种 a/b、粒度、顺序和质量元数据。 | Adapter 接口只接受 `PreparedAcquisition` 并产出 `ReceiverObservationChunk`；不暴露寄存器/ADC/IQ。用户已明确“底软负责逻辑扫描，上层开发用 Mock”，但正式字段仍需底软契约签字。 |
| IAC-027 | `a/b` 必须带 source port、receive port、波方向和参考条件；厂商名称不能直接移植为公司板卡语义。 | K7 的 A/B/R 是 receiver 名，并由 source port 决定采集上下文。 | R1 明确 `ai Src Port j` 与 `bi Src Port j` 的 incoming/outgoing wave 和 reference/measurement receiver 含义。 | C7 的 A(n)、B(n)、R1(n)、R2(n) 中 `n` 是 stimulus port。 | **E1**：wave quantity 至少需要 receive/source identity。**E2**：A/B/R 与 ai/bi 的命名和方向约定不同。**E3**：规范化 WaveDescriptor。**E4**：power-wave/voltage-wave、Z0、单位、归一化、factory correction。 | `CapabilityDescriptor.wave_convention` 必填：label、direction、source/receiver path、complex unit、Z0、normalization、pre-applied corrections。任一未知时不得启用可比 S 参数或绝对 receiver 测量。 |
| IAC-028 | 商用文档会报告 invalid setting、warning 或 valid sample，但没有跨厂商公开的“逐点、逐接收路径质量位”契约。不得据此假设公司底软会给过载/失锁/缺点标志。 | K1 有无法满足设置、功率/settling 限制和 sweep warning；K5 只定义数据阶段，未定义统一 per-point quality array。 | R1 的 detector 只收集 valid results，并有硬件完整性 warning；未公开本项目所需的通用逐路径位图。 | CMT 公开 correction status 和 trigger state，但 C6 的数组没有通用 per-point quality metadata。 | **E2**：各家存在 warning/status，但公开粒度不同。**E3**：统一 `QualityCode`、validity mask 和非局部传播规则。**E4**：底软实际可提供的 overload/unlock/unleveled/missing-point 位。 | 质量信息不得从 NaN 猜测后覆盖原始原因。若底软只给整扫状态，Capability 必须声明 `quality_granularity=Sweep`；group delay/FFT/平滑等节点标注局部或非局部污染范围。 |

## 四、Chunk、Manifest 与完整多端口逻辑 Sweep：IAC-029 至 IAC-032

| ID | 逐项对齐结论 | Keysight 官方证据 | R&S 官方证据 | CMT 官方证据 | E1/E2/E3/E4 判定 | 架构处置与未决边界 |
|---|---|---|---|---|---|---|
| IAC-029 | 公开手册没有给出公司 Board Adapter 的 chunk ownership、sequence、buffer lease、backpressure 或迟到 callback 语义。CMT 的 Trace FIFO 是用户 memory trace 队列，不能当成采集 transport 证据。 | K5 说明仪器内部 data access point，不说明底软回调内存所有权。 | R3 是仪器内部数据流，不说明 SDK chunk transport。 | C6 的 FIFO 是 Data→Memory 后的 trace memory，且无 SCPI 直接访问；它不是板卡采集缓冲接口。 | **E3**：move-only lease、有界队列、单调 sequence、唯一 terminal、generation token。**E4**：底软 callback/threading、buffer 生命周期、最大 chunk、pause/abort 能力。无 E1/E2 可证明 transport 合同。 | `BoardRunSink` 明确 `on_chunk(seq, lease)`、phase 与一次 terminal；慢 Web 只丢/合并 Preview，绝不反压采集线程。所有阈值来自 Capability 和 RTOS 压测，不复制商用品内部猜测。 |
| IAC-030 | 一次完整网络结果可包含多个 source state 与多个 receiver vector；各家硬件执行方式不同。Manifest 是实现这种可追溯性的架构手段。 | K1/K7 说明二端口标准 sweep 依次完成 forward/reverse，并按 source port 形成不同参数。 | R5 的 ZNBT 可同步捕获多端口并并行处理，证明“源/接收拓扑决定执行计划”，但仅适用于该硬件。 | C10 展示 Port 1 forward 和 Port 2 reverse 两次 sweep 组合完整测量。 | **E1**：完整矩阵需要覆盖所需 source directions/receiver observations。**E2**：顺序、并行度和硬件拓扑不同。**E3**：版本化 PreparedExecutionManifest。**E4**：公司板卡 source states、receiver vectors、辅助观测和 resource lease。 | PreparedExecutionManifest 至少含 actual axis、source state、route、receiver vector、trigger policy、chunk contract、required/optional observation 和 resource bounds。协议不能越过 Compiler/prepare 直接要求“只扫一半然后标完整”。 |
| IAC-031 | 完整二端口必须同时具备 forward 与 reverse 条件；“相同实际轴、同 revision、原子 bundle”是为了正确组合而采取的 E3 规则，厂商手册未证明公司底软已保证。 | K1 明确标准 sweep 先扫全部 forward points，再扫全部 reverse points；即使未显示 reverse trace，full 2-port correction 也可能要求 reverse sweep。 | R1 的 S 参数/wave 定义要求 source port 区分方向；R5 的同步多端口是另一种具体执行能力。 | C10 明确 Port 1 sweep 测 S11/S21，Port 2 reverse sweep 测 S22/S12。 | **E1**：full two-port 覆盖两个 source directions。**E2**：source-by-source、point-by-point 或并行硬件策略不同。**E3**：同 Manifest/revision 的完整 bundle 和 axis compatibility gate。**E4**：底软轴、切路和方向一致性。 | `prepare` 比较每个 sub-sweep 的实际 axis；能统一则记录映射，不能统一则拒绝。只在 bundle boundary 参与仲裁/平均/正式发布；内部 sub-sweep 不产生正式 Sij Snapshot。 |
| IAC-032 | 官方资料可证明 abort 和 completion synchronization 存在，但不能证明“任一 sub-sweep 失败时绝不更新任何结果”的跨厂商原子性。 | K12 说明必须等待具体 overlapped measurement 完成才能可靠读结果；K1 的 forward/reverse 是多 sweep 过程。 | R4 警告 continuous 模式会读到不同 sweep 的不一致值，支持本项目避免混合结果的必要性，但不是原子提交证明。 | C18 定义 abort 后的触发状态转换；C4 的 `*OPC?` 只在 pending operations 完成后返回，未定义 partial data publish。 | **E2**：各家 abort/等待细节。**E3**：required sub-sweep 任一失败则逻辑 Sweep 失败，不发布该 Sweep 的新 A/B 或以它为父的新 Live C，也不推进其对应 Average/Hold/Marker/Limit；基于旧 B 的重算以及 Frozen/Imported/Derived C 独立继续。**E4**：底软 abort SLA、失败回调和迟到数据行为。无 E1 可证明厂商内部原子提交。 | 保留 `last_good_snapshot_id` 并显式标 stale；失败/取消的 Preview 丢弃，不补零、不混用旧方向。若未来交付 partial matrix，必须建独立 Measurement 类型和完整 validity matrix，不能放宽默认语义。 |

## 五、Preview 与完成边界（跨 IAC-005、014、015、017、020、031、032）

| 可观察行为 | 官方依据 | 判定 | 本项目处置 |
|---|---|---|---|
| 扫描中显示进度/当前点 | K1 的 point sweep 逐点更新 trace 和 sweep indicator；C8 的 current stimulus position indicator；R4 描述 continuous manual mode 即时反映 DUT 调整。 | **E1**：商用品允许 in-progress visual feedback。 | 产生有 revision、operation ID、sequence 和 completeness 的限速 `SweepPreview`；它不是 Measurement Snapshot。 |
| 命令返回不等于测量完成 | K12 把 measurement 列为 overlapped operation；C4 的 `*OPC?` 等待 pending operation；R4 要求 single sweep completion synchronization。 | **E1**：必须有独立 completion barrier。 | Web 等 `Operation.completed`；SCPI `*OPC?/*WAI` 捕获同一 operation fence。协议层不自行轮询数据长度判断完成。 |
| 连续扫描时直接读取可能混轮 | R4 明确 continuous sweep 的 measurement values 可能来自不同 sweeps，且 `INIT;*OPC?` 不代表 continuous sweep end。 | **E2**：R&S 明确警告，其他来源没有同等措辞。 | 正式查询接受时固定 A/B/Stage/C 中与查询类型匹配的 typed result closure；`QueryTicket` Ready 与 `ResultPinLease` 同批取得，传输期间由 `ReaderLease` 保活，下一轮完成不改变正在传输的响应。 |
| 失败/取消的 Preview 是否可被 Marker、Limit、保存使用 | 三家资料没有公开统一的 snapshot promotion/rollback 合同。 | **E3**：一致性与故障隔离策略。 | 只有 successful logical Sweep 的 terminal 路径能原子 publish；Marker、Limit、Hold、Memory、保存和正式 SCPI data 默认只读 Completed。 |

## 六、`IAC-001..004`、`IAC-033..038` 补充证据投影

主表覆盖 `IAC-005..032`；下表把其余非校准行投影到对象/控制研究主题。`IAC-039..046` 则在 [`official-vna-calibration-processing-evidence.md`](official-vna-calibration-processing-evidence.md) 的 CAL-01..14 中逐项覆盖。

| 正式 ID | 证据主题 | E1/E2/E3/E4 边界 | 当前状态 |
|---|---|---|---|
| IAC-001 | 控制证据 DIA-01/02/04、SCPI 状态模型 | E2 厂商 Ready/错误/自检外部面；E3 Instrument 生命周期与 Quarantine；E4 实际自检范围 | 已由证据定案 |
| IAC-002 | DIA-01、CAP-04 | E2 capability/ready 外部面；E3 versioned descriptor；E4 公司底软 ABI 与真实性 | 待底软/硬件确认 |
| IAC-003 | 对象证据 OBJ-01、SEL-01/03、OWN-01 | E1 多 Channel；E2 selection/编号；E3 stable typed ID 与历史保留 | 已由证据定案 |
| IAC-004 | OBJ-02、C15 | E1 Channel 是采集设置主作用域；E2 override 差异；E3 Channel aggregate | 已由证据定案 |
| IAC-033 | 本文 Preview/Completed 边界、对象证据 SYN-03 | E1 进行中反馈与完成同步；E3 Preview 不晋升、A/B/C 三层发布 | 已明确 |
| IAC-034 | IAC-015、IAC-029、控制证据 CAP-02/04 | E1/E2 actual readback；E3 Compiler intent/claim→epoch pre-admit→prepare Manifest→exact reserve→lease upgrade→start；E4 底软量化/内存上界/拒绝 | 待底软/硬件确认 |
| IAC-035 | IAC-030/031、CAP-04 | E2 厂商硬件并行度不同；E3 ResourceGraph/unknown-default-exclusive；E4 公司共享资源矩阵 | 待底软/硬件确认 |
| IAC-036 | IAC-014、对象证据 SYN-04 | E2 Continuous/Single 行为；E3 公平队列、bundle 边界让出和 lease；无厂商内部调度事实声称 | 已由证据定案 |
| IAC-037 | 对象证据 SYN-01..04、控制证据 SCPI-09/WEB-E06 | E1 start 与 completion/synchronization；E2 pending 集合；E3 Operation Catalog/fence | 已由证据定案 |
| IAC-038 | IAC-029/032、CAP-04、DIA-03 | E2 Abort 外部行为；E3 generation/terminal/quarantine；E4 out-of-band abort SLA 与 RF safe-state | 待平台验证 |

## 架构处置建议

### 1. 配置、准备、Preview 与 A/B/C 三层正式结果必须分开

```text
ChannelConfigRevision
        ↓ prepare/capability validation
PreparedExecutionManifest
        ↓ Board Adapter + logical sub-sweeps
SweepPreview (0..N, 可丢、不可晋升)
        ↓ one successful terminal commit
CompletedSweepBundle (A 层完整采集)
        ↓ RF/average/user-correction
CompletedMeasurementBundle (B 层正式测量，可保存/网络数据查询)
        ↓ AnalysisTrace revision 求值
AnalysisPublication (C 层逐 Trace Marker/Limit/格式求值)
```

`ChannelConfigRevision` 保存用户意图；`PreparedExecutionManifest` 保存实际轴、实际/请求参数、路由和 trigger policy；Preview 只做运行反馈；A 层 `CompletedSweepBundle` 是完整采集事实，B 层 `CompletedMeasurementBundle` 才是原生 Sweep completion fence，C 层 `AnalysisPublication` 独立失败隔离。把这些合成一个可变对象会重现 R&S 官方提示的 mixed-sweep 问题。

### 2. Sweep Compiler 以“所需观测”编译 Intent，Board prepare 才形成 Manifest

输入为 Channel revision 下所有 live `MeasurementSpec`，Compiler 输出含去重 source states、receiver vectors、segments、trigger consumption 和保守资源声明的 `SweepIntent + ConservativeResourceClaim`。一个 source state 的 `a/b` 可以服务多个 Sij/receiver Trace；新增 Trace 若不增加观测，不应增加硬件 sweep。Arbiter 先按 ResourceGraph topology epoch 保守预准入，Board `prepare` 再把 intent 量化成 `PreparedExecutionManifest`；只有按 Manifest 完成校准/能力验证、精确容量预留并把 token 升级为 `ExecutionLease` 后才能 start。Intent、claim 与 Manifest 都必须可序列化，便于 Mock golden test 与真实底软 trace 对照。

### 3. Trigger 是调度状态机，不是协议线程里的条件变量

统一状态至少包含 `Idle/Hold → Armed → WaitingTrigger → Acquiring → Finalizing → terminal`。Web、SCPI 和硬件 edge 只提交 Trigger Command/Event；scope、granularity、finite count 和 average repeat 都由同一状态机消费。外部 trigger 引脚能力完全由 Board Profile 提供。

### 4. Average 必须把输入 stage 与 sample boundary 分开写进结果元数据

建议 `AveragePolicy` 至少分开定义：

- `input_stage`：`ReceiverWaves`、`MeasuredRatio`、`FactoryCorrectedRatio` 或 `CorrectedNetwork`；其中 factory/port correction 与用户校准 Error Terms 必须分型；
- `sample_boundary`：`Point`、`SourceState` 或 `LogicalSweep`；
- `mode/factor`：finite、running/sliding（若兼容目标要求）、factor、clear generation；
- `trigger_consumption`：一次 trigger 消费一个 point、source state、logical sweep 或 factor repeats。

项目原生推荐 `MeasuredRatio + LogicalSweep`；Keysight ENA Profile 可使用 `FactoryCorrectedRatio + LogicalSweep`，随后才应用用户 Error Terms；CMT 内部数组 Profile 使用 `ReceiverWaves + LogicalSweep/Point` 并要求目标固件回归。R&S/其他 Profile 只有在目标资料与黄金回归证明后才使用 `CorrectedNetwork`。Snapshot 必须记录 stage、boundary、factory/user correction 边界、mode、factor、generation、每点有效 count 和算法版本；显示格式不参与 Average。

来源证明必须有界：finite factor 可显式列出受 ProductProfile 上限约束的 `source_sweep_ids`；sliding 只保留固定窗口 IDs 与含 aggregate sums/weights、逐点 count/quality、实际轴的 `AverageAccumulatorSnapshot`；cumulative/running 只保存 accumulator snapshot ID、generation/count、首末输入序号范围和滚动强摘要。逐 Sweep 审计明细可以按独立、有限 retention 保存，不能塞进每个 B 层快照无限累积。

### 5. 数据阶段使用不可混淆的类型

建议公开/内部统一以下阶段标签：

```text
ReceiverObservation
RawReceiverWaves
RawMeasurementComplex
CorrectedMeasurementComplex
ProcessedComplex
FormattedTraceValues
```

是否允许 Web/SCPI 访问每一层由权限和 Compatibility Profile 决定，但内部不可用一个 `vector<complex>` 加注释替代阶段类型。Marker/Limit 默认消费固定 C 层 `AnalysisPublication` 的 formatted/derived view；校正消费 B 层及其处理图中的复数阶段，Live Trace math 可从 B 层进入，Frozen/Imported/Derived/Memory/Accumulator math 则消费 `AnalysisInputRefSet` 中对应的 typed immutable inputs。

### 6. Board Adapter 只承诺可验证的最小合同

Real、Mock、Replay Adapter 共享同一逻辑接口：`describe`、`prepare(BoardCallId, ...)`、`request_prepare_abort`、`start`、`request_abort`、`request_safe_state`、`request_emergency_kill`、`health`、`recover`。采集输出带 Manifest ID、generation、sub-sweep/source-state ID、axis range、receiver path、sequence、quality 和唯一 terminal；prepare abort 仅表示请求，唯一 prepare terminal 来自受监控 job 返回。任何 ADC/IQ、寄存器和厂商 SDK 结构都留在 Real Adapter 内。

### 7. Preview 与正式查询走不同 QoS

采集线程写固定池和有界内部队列；Preview broker 可以抽样、合并或让客户端 resync，慢浏览器不得占有底软 Buffer Lease。A/B/Stage/C typed 正式结果原子发布后，Web 与 SCPI 都通过 Snapshot Catalog 读取；查询接受时建立 `QueryTicket`，Ready 与完整 `ResultClosure` 的 `ResultPinLease` 同批取得，`open_read` 转为 `ReaderLease` 并保活到传输完成。该流程是为落实官方完成/一致性行为而采用的 E3 设计，不声称厂商内部使用相同对象。

## 逐项矩阵处置后的剩余门禁

### Compatibility / Board Profile

1. 普通运行中改参已经冻结为下一完整轮生效；只有哪些 RF 安全/重启字段必须 cancel/restart 仍由 Board Profile 声明。
2. 主 SCPI 方言及 Trigger Profile 仍需冻结：Keysight `Groups`、Global/Channel、whole-sweep/point 的精确计数边界。
3. Core Segment 已冻结为单调不重叠；Keysight arbitrary/reverse/overlap 作为 Compatibility/Pro capability。
4. 核心词汇已分开 `CW Point`、`CW Time` 与 `VNA Time Domain Transform`；厂商 SCPI 别名进入 Compatibility Profile。
5. Average 的 input stage/sample boundary，以及达到 factor 后 fixed/running/sliding/Single-auto-clear 行为进入 Compatibility Profile。
6. Power Sweep 模型已经存在，但真实执行只由 Board Capability 开放；无硬件证据时返回 unsupported。
7. 必需 sub-sweep 失败默认不发布部分矩阵；若未来产品确需 partial matrix，必须另建显式数据类型、validity matrix 和 SCPI 行为。

### 只剩硬件/底软 E4 决策与实测

1. 端口、source、reference/test receiver 数量，logical-to-physical route、并行/互斥和切换成本。
2. frequency/power/IFBW/points/segments 的范围、离散档位、量化、actual readback 与 timing 精度。
3. `a/b` 的数学波定义、方向、单位、Z0、归一化、每个 label 对应的 source/receiver path，以及底软已经应用的 factory correction。
4. 完整二端口/多端口逻辑扫描由谁编排；forward/reverse 次序、逐点还是逐源、轴一致性、辅助观测和一次 trigger 的实际消费边界。
5. 底软内部 Average 是否存在、作用域/算法、能否关闭、factor 和 counter 是否可读。
6. trigger input/output 的 connector、电平、极性、delay、minimum pulse、rearm 和逐 point/segment 能力。
7. 每个 chunk 的最大尺寸、顺序、线程、buffer ownership、backpressure/暂停能力、terminal 保证、abort SLA 与迟到 callback 行为。
8. overload、unlock、unleveled、missing point、timeout 等质量信息的实际位定义、粒度和版本化方式。

在上述 E4 资料到位前，Mock 应覆盖多套明确 Profile，而不是把某一套猜测固化成“真实板卡”：最小一端口、完整二端口、离散 IFBW/功率量化、逐 source-state trigger、逐 point trigger、receiver-domain average、乱序/缺块/迟到 terminal 和不可中断 abort。
