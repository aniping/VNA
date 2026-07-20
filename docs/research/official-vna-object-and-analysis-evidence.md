# 商用 VNA 对象、分析与控制行为的一手证据

## 1. 研究问题与证据边界

本研究回答：Keysight PNA/ENA、Rohde & Schwarz ZNA/ZNB、Copper Mountain VNA 的官方资料，对 Instrument、Channel、Measurement、Trace、Diagram/Window、Marker、Limit、活动/选择上下文、扫描完成同步以及删除/所有权公开了哪些**可观察行为**，这些证据能否支撑当前 VNA 软件的候选规则。

只使用厂商官方用户手册、编程手册、命令参考和厂商发布的远程控制说明。不使用博客、论坛、第三方封装或对厂商内部实现的推测。厂商资料中的对象名不是统一标准：例如 Keysight 明确区分 Measurement 与显示 Trace，R&S 和 CMT 的 Trace 往往同时承载“测量量选择”和一部分分析/显示设置。因此，下表比较的是外部行为，不强行统一厂商术语。

> **R&S 版本冻结。** 本基线固定引用 ZNA User Manual v39 与 ZNB/ZNBT User Manual v72；截至研究日，R&S 官方入口已发布 [ZNA v40（2026-03-27）](https://www.rohde-schwarz.com/uk/manual/rs-zna-user-manual_78701-601863.html) 和 [ZNB/ZNBT v73（2026-04-14）](https://www.rohde-schwarz.com/ca/manual/r-s-znb-znbt-user-manual-manuals_78701-29151.html)。升级参考版本时必须重新核对章节、命令和兼容回归，不能把 v39/v72 称为当前最新版；本文页码均指 PDF 页脚页码。

### 外部行为证据

以下内容可以由操作者或远程客户端直接观察，因而属于证据范围：

- 哪些设置以 Channel、Measurement、Trace、Window/Diagram、Marker、Limit 为命令或对象作用域；
- 对象如何创建、选择、显示、移动、隐藏和删除；
- Marker/Limit 读取、搜索、Pass/Fail 与显示开关的关系；
- `INIT`、Single/Continuous、`*WAI`、`*OPC`、`*OPC?` 的等待和完成语义；
- 命令使用的是活动对象、每 Channel 选择对象还是显式对象编号。

### 不属于厂商外部证据的内部架构推导

下列候选规则即使合理，也不能仅凭厂商命令树宣称为商用品内部事实：

- `Instrument Kernel`、单写者、Command/Query/Event、revision、不可变 Snapshot 的具体实现；
- `MeasurementDefinition`、`TraceDefinition`、`TracePresentation` 是否必须是三个持久化 C++ 实体；
- 每个 Web/SCPI TCP 连接是否拥有完全独立的 selected Channel/Trace/Marker；
- 对象 ID 永不复用、历史结果的引用计数和垃圾回收政策；
- Marker/Limit 具体位于哪一个处理图节点，以及是否以同一算法同时服务 Web 与 SCPI；
- Continuous Sweep 在内部是否由一个还是多个 Operation 表示。

“能否支持当前候选规则”采用三档判断：

- **直接支持**：厂商明确公开了与候选规则相同的可观察行为；
- **部分支持**：证据支持该分层或约束的必要性，但不规定我们的内部对象形状；
- **不能证明**：一手资料没有公开该语义，或厂商公开行为与候选规则存在兼容风险。

## 2. 逐项证据矩阵

本表的 `OBJ/VIEW/MRK-E/LIM-E/SEL/SYN/DEL/OWN` 是**研究主题 ID**，不是 176 项总矩阵的正式功能 ID；`MRK-E`、`LIM-E` 前缀特意避免与 `ANA-xx` 正式行重名。正式状态与验收入口仍以 [`feature-alignment-matrix.md`](../design/feature-alignment-matrix.md) 及三份 `docs/design/alignment/` 明细为准。

R&S 主证据定位如下：ZNA v39 §4.1.3 p.113 定义 Trace、Channel、Diagram 的关系并明确 Diagram 可显示来自不同 Channel 的 Trace；§4.1.3.3 p.115 区分手动操作的全机 active trace 与远控语境下的每 Channel active trace；§3.3.5.3 p.59 给出 Marker、Trace、Diagram、Channel 及 PASS/FAIL 显示的删除副作用；§4.2 p.138 给出 Marker peak/target/bandfilter search 与 tracking；§4.4.1 p.175 给出 upper/lower/ripple/circle limit 与 PASS/FAIL。它们证明的是外部行为，不证明本项目的内部 ID、revision、不可变历史或每 TCP 连接隔离选择上下文。

| 主题 | Keysight 证据 | R&S 证据 | CMT 证据 | 跨厂商结论 | 厂商差异 | 能否支持当前候选规则 |
|---|---|---|---|---|---|---|
| OBJ-01 Instrument 管理多个 Channel | [COM Fundamentals](https://helpfiles.keysight.com/csg/N52xxB/Programming/Learning_about_COM/COM_Fundamentals.htm) 明确说 Channels collection 包含全部 Channel 对象；[Channel Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Channel_Object.htm) 可通过 `app.Channels(n)` 访问或新增 Channel。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 把 Channel management、Channel settings 和 Trace settings 分开，并说明每条 Trace 分配给一个 Channel。 | [Channel Allocation](https://coppermountaintech.com/help-cmtvna/1-port/channel-allocation.html) 公开多个 Channel window、逐 Channel 刺激/校准及顺序执行行为。 | 三家都把多 Channel 作为一等外部概念，而不是若干互不相关的窗口。 | CMT 将 Channel 与屏幕 Channel window 紧密绑定；Keysight 可创建没有可见效果的 Channel；R&S Diagram 可跨 Channel 显示。 | **直接支持** Instrument 下管理多个 Channel；不证明内部容器或持久化方式。 |
| OBJ-02 Channel 是采集设置作用域 | [Channel Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Channel_Object.htm) 把频率、功率、IFBW、点数、Sweep、Trigger、Average 和 CalSet 放在 Channel。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 定义 Channel settings 为测试装置、Sweep/Trigger/Average 和 correction data，并明确适用于该 Channel 全部 Trace。 | [Channel Allocation](https://coppermountaintech.com/help-cmtvna/1-port/channel-allocation.html) 要求先选择 active Channel 再设置 stimulus 或 calibration；[Channel Initiation Mode](https://coppermountaintech.com/help-cmtvna/1-port/channel-initiation-mode.html) 将 Continuous/Single/Hold 作用于 Channel。 | “Channel 统一承载硬件采集设置，多个分析结果共享”是稳定交集。 | 各家哪些设置可在 Segment/Trace 覆盖不同；本结论只覆盖主作用域。 | **直接支持**当前 Channel 作为 acquisition scope 的候选规则。 |
| OBJ-03 Measurement 表示“测什么” | [Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm) 明确 Measurement 由 S11、A/R1、B 等 parameter 定义，并与驱动采集的 Channel 关联。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 把 measured quantity 选择归入 Trace settings，没有公开独立 Measurement 对象。 | [CALC:PAR:DEF](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcpardef.html) 在指定 Channel/Trace 上选择 S 参数或 receiver parameter，没有单独的 Measurement identity。 | 各家都能独立选择测量量，但只有 Keysight 明确公开独立 Measurement 对象。 | R&S/CMT 的外部 Trace 同时包含 parameter；照搬 Keysight 名词不能成为跨厂商兼容要求。 | **部分支持**内部保留“测什么”的独立类型；它究竟是实体还是 Trace 内值对象属于 E3 架构推导。 |
| OBJ-04 Measurement 与显示 Trace 分离 | [Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm) 说明 Measurement 把 Channel 原始数据处理到可显示阶段，随后成为 Trace scope；[Trace Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Trace_Object.htm) 只控制 scale、reference position/value 等显示。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 把 measured quantity、format、diagram type、scale、Marker、Limit 都列为 Trace settings，未公开 Keysight 式二分。 | [CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 把 parameter、format、math、Marker、Limit 都放在 Channel/Trace 路径；[Diagram](https://coppermountaintech.com/help-cmtvna/1-port/diagram.html) 再负责图形区域。 | “采集/测量数据”和“图形呈现”存在可观察分界，但对象切分粒度并不统一。 | Keysight 分界最强；R&S/CMT 的 Trace 是复合外部概念。 | **部分支持** Analysis Trace 与 Trace Placement 分离；具体对象形状是 E3 内部推导。 |
| OBJ-05 Trace 具有独立分析设置 | [Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm) 将 error correction、math、time domain、gating、format、Marker、Limit 置于 Measurement，而显示 Trace 主要管理比例尺。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 明确 Trace settings 包括测量量、格式、Marker 和 Limit。 | [CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 对指定 Trace 暴露 parameter、format、math、smoothing、Marker、Limit 和时域。 | Marker、Limit、Math 等分析行为不属于纯 Diagram 布局；它们跟随某个可测量/可分析结果。 | Keysight 将大部分分析叫 Measurement 行为，R&S/CMT 称为 Trace 行为。 | **直接支持**“分析 Trace 不能只是 Diagram 里的一条彩线”；内部命名仍需兼容映射。 |
| OBJ-06 Window/Diagram 是显示容器 | [Trace Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Trace_Object.htm) 可经 Window 的 Traces collection 获取；[Display SCPI](https://helpfiles.keysight.com/csg/NA520xA/Programming/GP-IB_Command_Finder/Display.htm) 用 FEED 把 Measurement 接到指定 Window 的 Trace，并可移动 Trace。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 明确 Diagram 是显示 Trace 的矩形屏幕区域，Diagram 设置独立于 Trace 和 Channel。 | [Diagram](https://coppermountaintech.com/help-cmtvna/1-port/diagram.html) 定义 Diagram 为 Channel window 内显示 Trace 和数值的 graph area；[DISPlay tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/display.html) 管布局、显示和比例尺。 | 三家都把 Window/Diagram 作为呈现容器，而不是采集数据本身。 | R&S Diagram 可跨 Channel；CMT Diagram 位于单个 Channel window；Keysight 通过 feed/move 把 Measurement 呈现在 Window。 | **直接支持** Diagram/Window 只负责呈现域的候选方向；不能仅凭此断言所有显示删除副作用。 |
| OBJ-07 一个 Diagram 是否可跨 Channel | [Display SCPI](https://helpfiles.keysight.com/csg/NA520xA/Programming/GP-IB_Command_Finder/Display.htm) 以 Measurement number/name 向 Window feed Trace，但具体跨 Channel 组合受型号/命令限制，文档没有在同一段给出通用承诺。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 明确一个 Diagram 可含大量 Trace，且 Trace 可来自不同 Channel。 | [Number of Traces](https://coppermountaintech.com/help-cmtvna/TR-Series/number-of-traces2.html) 只说明一个 Channel window 内的 Trace 可叠加或分配到不同 Diagram。 | 跨 Channel overlay 是已存在的商用品行为，但不是三家一致的布局模型。 | R&S 直接支持；CMT 的 Diagram 受 Channel window 约束；Keysight 需按目标型号验证。 | **部分支持**当前允许跨 Channel Diagram overlay；必须作为产品能力而非兼容共同最低线。 |
| MRK-E01 Marker 依附于可分析结果而非 Diagram | [Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm) 明确每个 Measurement 有自己的一组 Marker；[Marker Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Marker_Object.htm) 通过 Measurement 获取。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 将 Marker 读值和搜索列为 Trace settings；删除 Marker 是把它从 Trace 上释放。 | [CALC:MARK](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcmark.html) 的目标是 active Trace 或显式 Channel/Trace 的 Marker。 | Marker 的业务输入是某个 Measurement/Trace；Diagram 只呈现 Marker 符号和读数。 | Keysight 归属 Measurement；R&S/CMT 归属 Trace；编号和参考 Marker 规则不同。 | **直接支持外部归属**：Marker 面向可分析结果而非 Diagram；内部 `MarkerDefinition` 归属是 E3 架构决策。 |
| MRK-E02 Marker 读值、搜索与 Tracking | [Marker Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Marker_Object.htm) 公开 Stimulus、Value、Interpolated、SearchMax、Target、Peak、Tracking、Bandwidth 等行为。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 的 Marker 键公开位置、读值格式、搜索、Marker-to Sweep/Scale；R&S 旧代同系 VNA [ZVL Operating Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_common_library/dl_manuals/dl_user_manual/ZVL_OperatingManual_en_09.pdf) 明确极坐标搜索以 magnitude 为判据并可每 Sweep tracking。 | [CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 公开 discrete、X/Y、peak/target/search range、tracking、bandwidth、flatness 等命令。 | Normal/Delta、插值/离散、峰值/目标搜索、Tracking 和复合分析是成熟 VNA 的外部能力，不是画图装饰。 | 复杂格式的搜索 metric、相等峰 tie-break、Next 方向和 Marker 数量差异很大。 | **E1/E2** 支持 Marker 基础能力；**E3/E4** 仍需冻结 tie-break、输入投影和算法黄金数据。 |
| MRK-E03 Fixed Marker | Keysight [Markers](https://helpfiles.keysight.com/csg/N52xxA/S4_Collect/Markers.htm) 明确 Fixed Marker 固定设置时的 X/Y，不随 Trace data amplitude 移动，用于 before/after 比较。 | 本轮 ZNA 资料未找到同样明确的 Fixed 类型；不能把 Keysight 名称推广成三家共同方言。 | CMT 资料公开 Reference/Delta 和普通 Marker，但本轮未找到与 Keysight Fixed 完全同义的类型。 | 至少一类主流商用 VNA 明确提供“固定 X/Y 比较读数”；项目可把它作为独立 Marker kind。 | 命名、是否保存 Y、与 Delta/Memory 的关系存在方言差异。 | **E2** 支持 `MRK-05` 是明确商用能力但非跨厂商共同事实；内部 immutable fixed value/revision 为 **E3**。 |
| MRK-E04 Marker Coupling | Keysight [Markers](https://helpfiles.keysight.com/csg/N52xxA/S4_Collect/Markers.htm) 明确按 marker number 耦合，可选 Channel 或 All scope，并只在相同 X-axis domain 间耦合。 | 本轮 ZNA 资料未形成与 Keysight scope 相同的共同规则。 | [CALC:MARK:COUP](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcmarkcoup.html) 明确同 Channel 不同 Trace 的同编号 Marker 跟随 X 位置。 | 多 Trace Marker 共享刺激位置是明确商用能力。 | Channel/All scope、X-domain 检查、插值和 ON/OFF 联动不同。 | **E1/E2** 支持 `MRK-12`；项目只共享刺激意图、各 Trace 独立求实际点/值是 **E3** 安全归一化。 |
| MRK-E05 Marker Table 与批量读回 | Keysight [Markers](https://helpfiles.keysight.com/csg/N52xxA/S4_Collect/Markers.htm) 明确 Marker Table 汇总 active Trace 的 Marker 数据。 | 本轮 ZNA 资料只证明多 Marker readout，未找到与 Keysight 相同的批量远程表结构。 | [CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 列出 `CALC:MARK:DATA?`，返回全部已开启 Marker 的 stimulus/response 数据。 | 成熟产品需要一致的多 Marker 表格/批量查询。 | 表字段、排序、跨 Trace 汇总和协议编码不同。 | **E1/E2** 支持 `MRK-14` 的表格/批量查询；CSV 导出、同代 publication pinning 和分页是项目 **E3**，不是三家共同内部实现。 |
| LIM-E01 Limit 绑定 Measurement/Trace | [MeasureLIMit](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Calculate/MeasureLIMit.htm) 以 Channel/Measurement 为目标设置 segments、测试并查询结果。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 将 Limit check 列入 Trace settings，并由 Line 功能定义、显示和启停。 | [CALC:LIM:DATA](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calclimdata.html) 以 active Trace 或显式 Channel/Trace 为目标保存上/下限、single-point 和 off segments。 | Limit 的判定对象是一个 Measurement/Trace 结果，而不是 Diagram 像素。 | Keysight 以 Measurement 编号寻址；R&S/CMT 主要以 Trace 寻址；segment 数据结构与数量上限不同。 | **直接支持外部归属**：Limit 面向可分析结果；内部 Definition/Placement 切分是 E3 架构决策。 |
| LIM-E02 Limit 显示与测试可分离 | [MeasureLIMit](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Calculate/MeasureLIMit.htm) 分开 `LIMit:DISPlay:STATe` 与 `LIMit:STATe`，且 FAIL 查询不要求 limit display 打开。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 说明 Line 功能可定义/可视化并独立 activate/deactivate check；删除 PASS/FAIL 显示会隐藏线并停用 check，但不删除可复用 Limit Line。 | [CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 分开 `CALC:LIM`、`CALC:LIM:DISP`、`CALC:LIM:FAIL?` 和报告命令。 | Limit Definition/Test 与 overlay 可见性必须是不同状态；隐藏线不应自动等同删除定义。 | R&S 的拖拽删除 PASS/FAIL 同时隐藏并停用 check；Keysight/CMT 提供显式独立命令。 | **直接支持**测试开关与显示开关分离；UI 快捷操作可作为组合命令。 |
| LIM-E03 Limit 输出不仅是总 Pass/Fail | [MeasureLIMit](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Calculate/MeasureLIMit.htm) 可查询总 FAIL、逐点 test/upper/lower、失败点 stimulus 和失败点数。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 公开 PASS/FAIL 消息和 limit check，但本次查到的 ZNA 章节未给出与 Keysight 相同的逐点远程报告结构。 | [CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 提供 FAIL、all-point report、failed-point count 和失败 stimulus。 | 成熟实现至少要有总判定和可诊断的失败位置；Keysight/CMT 明确支持逐点/失败点报告。 | R&S ZNA 的精确远程报告字段仍需按目标固件 Remote Control Manual 补证。 | **直接支持**总判定与失败明细；margin、失败区间和 worst point 是合理扩展但不由三家共同证据完整规定。 |
| SEL-01 Front Panel Active 与每 Channel Selected | [SCPI object reference](https://helpfiles.keysight.com/csg/N52xxA/Programming/Learning_about_GPIB/Referring_to_Traces_Measurements_Channels_Windows_Using_SCPI.htm) 明确全机只有一个 Active Measurement，但每个 Channel 有一个 Selected Measurement，多数 CALC 设置前需选择。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 区分 active trace/channel/diagram；R&S 同系 VNA [ZVL Operating Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_common_library/dl_manuals/dl_user_manual/ZVL_OperatingManual_en_09.pdf) 明确 manual 全机一个 active trace，而 remote 每 Channel 一个 active trace。 | [Selection of Active Trace/Channel](https://coppermountaintech.com/help-cmtvna/1-port/selection-of-active-trace_channel.html) 明确 active Channel/Diagram/Trace，控制和删除前先激活；[CALC:PAR:SEL](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcparsel.html) 设置 Channel 内 active Trace。 | “前面板活动对象”和“远程命令默认目标”都是真实外部状态，必须明确作用域。 | Keysight 用 Active/Selected 两套词；R&S manual/remote active 可不同；CMT active 选择与 UI 更紧密。 | **直接支持**为协议实现显式 Selection Context；不支持把所有命令都改为无状态显式 ID。 |
| SEL-02 每个 TCP/SCPI 连接独立选择上下文 | Keysight 资料描述全机一个 Active Measurement、每 Channel 一个 Selected Measurement；未说明每个 Socket 会话各有副本。[CALC Parameter](https://helpfiles.keysight.com/csg/e5080a/programming/gp-ib_command_finder/calculate/parameter.htm) 还说明 `fast` selection 可不更新显示，表明选择与仪器显示有可观察耦合。 | R&S 资料描述 manual 与 remote active trace 的区别，但没有说明多个远程连接各自独立保存 active trace。 | CMT 文档把 `DISP:WIND:ACT`、`CALC:PAR:SEL` 与 Analyzer 当前 active Channel/Trace 对应，没有多连接隔离承诺。 | 一手资料没有形成“每连接独立 selected object”的共同外部行为；传统仪器更像共享选择状态。 | 不同 Transport、驱动 API 和固件可能有各自 Session 机制，本研究未找到模型级承诺。 | **不能证明，且有兼容风险。** Web selection 可采用 session-local；SCPI Compatibility Profile 必须决定共享仪器、每 Channel 共享或连接局部选择。 |
| SEL-03 对象编号作用域 | [SCPI object reference](https://helpfiles.keysight.com/csg/N52xxA/Programming/Learning_about_GPIB/Referring_to_Traces_Measurements_Channels_Windows_Using_SCPI.htm) 明确 Measurement Name、Window 内 Trace Number 和全机唯一 Measurement Number 是三种不同标识。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 分别呈现 Channel、Trace、Diagram、Marker 编号/标签；目标 Trace 归属 Channel。 | [CALC:PAR:SEL](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcparsel.html) 的 Trace number 在 Channel 路径下；[DISPlay tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/display.html) 另有 active Channel 和窗口布局。 | 不能用一个无作用域裸整数同时表示 Channel、Measurement、Window Trace、Marker。 | 唯一性和自动分配方式不同；Keysight 尤其存在多个容易混淆的 Trace/Measurement 编号。 | **直接支持**类型化 ID 和作用域校验；**不能证明**ID 永不复用。 |
| SYN-01 Sweep 启动与完成是不同事件 | [Keysight Synchronization](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm) 明确 `INITiate:IMMediate` 是 overlapped command，Measurement 完成前可继续处理后续命令。 | [R&S Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html) 强调测量同步点与动态等待；[1EF62](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ef62/1EF62_1E.pdf) 区分 Single/Continuous 并用 `INIT;*OPC?` 等待。 | [TRIG:SING](https://coppermountaintech.com/help-r/trigsing.html) 说明触发条件不满足会报错，并可配合 `*OPC?` 等待 sweep end。 | 接受启动命令不等于结果已经完成；正式分析/读取必须有完成同步。 | CMT 链接属于其 RVNA/RNVNA 产品族，命令细节只能作为型号族证据；Keysight 明确 INIT overlapped。 | **直接支持外部行为** start/terminal separation；Operation 和 completed-result fence 的具体对象模型属于 E3。 |
| SYN-02 `*WAI`、`*OPC?` 与 `*OPC` | [Keysight Synchronization](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm) 分别说明 `*WAI` 阻止后续命令、`*OPC?` 等 pending completion 后回复 1、`*OPC` 完成后置 ESR bit；PNA 还可有专有第二 TCP control connection。 | [R&S Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html) 给出 `*OPC?`、STB polling、SRQ 等机制，并明确 R&S RawSocket 可用 `*OPC?` 但没有 VISA control-channel ReadSTB/SRQ。 | [CMT *OPC?](https://coppermountaintech.com/help-cmtvna/Programming-Manual/opc_question.html) 等待所有之前指令完成并可等待 `TRIG:SING` Sweep end。 | `*OPC?` 必须等待真实 pending work，而不是“命令已入队”；异步 SRQ/control channel 是 Transport/Profile 能力，不能从 raw Socket 一概推导。 | pending 集合范围、Continuous 语义、control channel 和校准异步命令各家不同。 | **直接支持外部同步**；SRQ 是 E2 Transport 差异，具体 Operation/Session fence 集合属于 E3 兼容设计。 |
| SYN-03 Marker/分析必须在完整 Sweep 后执行 | [Keysight Synchronization](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm) 给出反例：未等待 `INIT` 完成就做 Marker max search 可能返回不准确值；插入 `*WAI` 后才正确。 | [R&S Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html) 要求在 acquisition-finished 同步点之后读取结果；[1EF62](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ef62/1EF62_1E.pdf) 警告 Continuous 下结果可能来自不同 Sweep。 | [CMT *OPC?](https://coppermountaintech.com/help-cmtvna/Programming-Manual/opc_question.html) 用于等待 Sweep end；Marker/Limit 命令树本身未公开“在扫中查询返回哪一代数据”。 | 正式 Marker、Limit 和数据读取必须能绑定一个已经完成的一致结果；否则结果代次不可证明。 | Keysight 提供 Marker 具体错误示例；R&S 说明一致性风险；CMT 只明确完成等待机制。 | **直接支持**正式分析只消费 completed result；**部分支持**“不可变 Completed Snapshot”这一具体内部实现。 |
| SYN-04 Single 与 Continuous 的同步边界 | Keysight [Synchronization](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm) 把 `INIT`/Groups 作为 overlapped operation，并另述平均完成；具体 Continuous fence 需按命令模式处理。 | R&S [1EF62](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ef62/1EF62_1E.pdf) 明确 Continuous 结果可能混合不同 Sweep，且 `INIT;*OPC?` 在 Continuous 下会立即返回、与 Sweep completion 无关，推荐远程使用 Single。 | [Channel Initiation Mode](https://coppermountaintech.com/help-cmtvna/1-port/channel-initiation-mode.html) 定义 Continuous 每次结束后重新 initiated、Single 结束后进入 Hold、Hold 不更新。 | Continuous 下不能假定 `*OPC?` 必然等待一轮完成；远程确定性读取需要目标方言规定的具体同步方法。 | 各家启动命令和 Continuous 下 `OPC` 的精确定义不同。 | **E2 厂商差异**；内部如何为 Continuous 建模和排除后来无关 Sweep 属于 E3。 |
| SYN-05 Measurement 携带最近采集刺激描述 | [Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm) 明确每个 Measurement 携带最近一次采集时 Channel stimulus 属性的 snapshot，Channel 后续改变后不再能准确描述旧数据。 | ZNA 本次检索到的章节明确 Channel/Trace 分层，但没有找到同等明确的“结果携带旧 stimulus snapshot”公开句子。 | [CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 可分别查询 X-axis、corrected/formatted data，但没有公开持久的结果代次/配置 snapshot。 | 至少 Keysight 明确证明“当前配置”和“产生已有结果时的配置”必须可区分。 | R&S/CMT 可能内部保留相应信息，但公开命令不构成证据。 | **部分支持**结果绑定 axis/config revision；不能据此声称三家都有不可变历史 Snapshot。 |
| DEL-01 隐藏/删除显示 Trace 不等于删除 Measurement | [Display SCPI](https://helpfiles.keysight.com/csg/NA520xA/Programming/GP-IB_Command_Finder/Display.htm) 明确 Trace display OFF 时背后的 Measurement 仍 active；删除 Window Trace 时 associated Measurement parameter 不删除。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 公开独立删除 Marker、Trace、Diagram、Channel，但其 Trace 本身同时包含 measured quantity，不能等同 Keysight display-only Trace。 | [Quick Access Toolbar](https://coppermountaintech.com/help-cmtvna/1-port/quick-access-toolbar.html) 可删除 active Trace；CMT Trace 同时带 Measurement parameter，未找到“删除显示而保留独立 Measurement”的承诺。 | 显示对象与测量定义的删除可以分开，但只有 Keysight 对该差别给出直接且明确的命令语义。 | R&S/CMT 删除 Trace 可能就是删除该外部测量结果定义；它们没有 Keysight 式 display trace/measurement 二分。 | **部分支持** DeletePlacement 与 DeleteAnalysisTrace 分开；SCPI 兼容层必须按方言选择或组合。 |
| DEL-02 显式删除 Measurement/Trace 定义 | [CALC Parameter](https://helpfiles.keysight.com/csg/e5080a/programming/gp-ib_command_finder/calculate/parameter.htm) 提供按 name 删除 Measurement 及删除全部 Measurement；[Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm) 也有 Delete method。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 删除 Trace 会从显示/配置中移除；最后一条 Trace 不允许删除。 | [CALC:PAR:COUN](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcparcoun.html) 通过减少 Channel 的 Trace 数删除尾部 Trace，范围最少为 1；工具栏可删除 active Trace。 | 分析结果定义的删除是独立操作，并常有“至少保留一个”或范围限制。 | Keysight 可存在不显示的 Measurement；R&S/CMT 更紧密耦合 Trace 与外部测量定义。 | **直接支持**显式区分删除命令；引用拒绝、cascade 和 tombstone 是我们的内部政策，需另定。 |
| DEL-03 删除 Marker 与 Limit 的副作用 | [Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm) 提供 DeleteMarker/DeleteAllMarkers；[MeasureLIMit](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Calculate/MeasureLIMit.htm) 可单独删除某 Measurement 的全部 Limit data。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 明确删除 Marker 同时禁用 associated marker function；拖走 PASS/FAIL 会隐藏线并停用 check，但 Limit Line 本体保留复用。 | [CALC:MARK](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcmark.html) 的 Marker ON/OFF 有编号联动；[CALC:LIM:DATA](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calclimdata.html) 设置 N=0 清除 Limit Line。 | Marker、Marker Function、Limit Definition、Limit Test enable 和 display visibility 是可区分状态，删除/关闭副作用必须显式规范。 | 各家编号联动和 UI 快捷组合行为不同。 | **直接支持**定义/求值/呈现状态分开；具体 cascade 规则应放 Compatibility Profile。 |
| DEL-04 删除 Diagram/Window | [Display SCPI](https://helpfiles.keysight.com/csg/NA520xA/Programming/GP-IB_Command_Finder/Display.htm) 可关闭 Window，并可在 Window 间 move/feed Trace；对单个 Trace 的删除明确不删 Measurement。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 可独立删除 Diagram，最后一个 Diagram 不能删除；该删除章节没有明确承诺其中 Trace 定义在所有情形下保留。 | CMT [Number of Traces](https://coppermountaintech.com/help-cmtvna/TR-Series/number-of-traces2.html) 通过 allocation 把 Trace 叠加或分配到不同 Diagram，未找到独立持久 Diagram 删除后 Trace 保留的命令承诺。 | Diagram/Window 有独立布局生命周期，但“删 Diagram 后分析 Trace 一律保留”不是三家共同明确语义。 | Keysight 最能支持 display reference 与 Measurement 分离；R&S/CMT 的具体副作用需按产品行为验证。 | **部分支持**当前“Diagram 不拥有分析对象”；最终删除政策仍需产品验收，不可仅凭一手资料宣布跨厂商一致。 |
| DEL-05 删除 Channel | Keysight [Channel Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Channel_Object.htm) 说明 Measurement 消失后 Channel 仍可能存在，并可创建不可见 Channel；未找到统一“删 Channel 自动删哪些 Measurement/Window”的直接段落。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 通过删除 Channel 关联的全部 Trace 删除 Channel，最后一个 Channel 不能删除。 | [Quick Access Toolbar](https://coppermountaintech.com/help-cmtvna/1-port/quick-access-toolbar.html) 可删除 active Channel；[Channel Allocation](https://coppermountaintech.com/help-cmtvna/1-port/channel-allocation.html) 把 Channel 与 window/测量执行紧密联系。 | Channel 删除会影响它的当前分析/显示对象，但跨厂商 cascade 细节不一致且资料不完整。 | Keysight Channel 可脱离可见 Measurement 存在；R&S 删除所有关联 Trace 来删除 Channel；CMT 直接删除 active Channel window。 | **不能证明**当前引用拒绝/cascade/历史保留矩阵；必须将每种命令的可观察结果写成项目规则并测试。 |
| OWN-01 历史结果、不可变快照与 ID 不复用 | Keysight [Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm) 只明确 Measurement 保存“最后一次采集”的刺激 snapshot；[Data to New Trace](https://helpfiles.keysight.com/csg/N52xxB/S4_Collect/Math_Operations.htm) 可复制出不再随新 Sweep 更新的独立 memorized trace。 | R&S ZNA 手册公开 stored trace、memory 和 recall 行为，但本次对象章节未证明统一的 immutable snapshot/ID retention 模型。 | CMT 公开 data/memory trace 和文件功能，但没有公开历史 Snapshot identity 或 ID 不复用政策。 | 商用品支持 last result、memory/stored trace 和状态保存，但未公开我们的历史所有权/保留算法。 | memory trace、stored trace、measurement data 与 state file 的生命周期各家不同。 | **不能证明。** 不可变 Snapshot、history retention、ID 不复用仍是可靠性设计决策，应由并发/审计/SCPI 一致性需求单独论证。 |

## 3. `02-analysis-display.md` 的 66 项逐 ID 证据投影

下表是正式功能 ID 到研究主题的审计索引。“已由证据定案”表示相关外部事实已按 E1/E2 边界记录（如有），项目原生语义、E3 设计和验收路径已闭合；它既不要求每项都是跨厂商 E1 共性，也不表示厂商公开了相同内部实现。尚未冻结的方言副作用、算法计量、E4 或产品范围仍按各自状态保留门禁。

| 正式 ID | 一手证据/研究主题 | E1/E2/E3/E4 边界 | 当前状态 |
|---|---|---|---|
| ANA-01 | OBJ-03/04/05 | E1 测量量与显示对象可分；E2 R&S/CMT Trace 更复合；E3 `AnalysisTrace(TraceSourceSpec)` | 已由证据定案 |
| ANA-02 | SEL-03、OWN-01 | E1 外部标识有作用域；E3 稳定 typed ID、revision、retention | 已由证据定案 |
| ANA-03 | SYN-03/05、OWN-01 | E1 正式读数需 completed result；E2 仅 Keysight 明示 last-acquired stimulus；E3 三层不可变快照 | 已由证据定案 |
| ANA-04 | OBJ-05、MTH-01/02/04、文件导入证据 | E1 Live/Memory/Math/frozen/imported 行为存在；E3 typed source variants 与不重扫重算 | 已由证据定案 |
| ANA-05 | OBJ-04/06、DEL-01 | E1/E2 feed/move/隐藏行为；E3 `TracePlacement` | 已由证据定案 |
| ANA-06 | OBJ-06/07、DEL-04 | E1 Diagram 是显示容器；E2 跨 Channel/删除不同；E3 不拥有 AnalysisTrace | 已由证据定案 |
| ANA-07 | OBJ-06/07、SEL-01/02 | E1 layout/active 对象；E2 selection 作用域；E3 Web session focus | 已由证据定案 |
| ANA-08 | OBJ-07 | E2 R&S 明示跨 Channel、CMT 受 Channel window 限制；E3 宽松 Core + Profile 约束 | 已由证据定案 |
| ANA-09 | OBJ-05、厂商 format/data-flow、MTH-07 | E1 格式能力；E2 phase/group-delay/format stage 差异；E3 typed projection；E4 公式黄金集 | 待算法/计量验证 |
| ANA-10 | OBJ-05/06、厂商 Smith/Polar format、MTH-07 | E1 Smith/Polar 显示读数；E2 Z0/readout 差异；E3 coordinate 与 projection 分离；E4 黄金点 | 待算法/计量验证 |
| ANA-11 | OBJ-04/06、CMT/Keysight display scale | E1 scale/reference 是显示控制；E3 Placement scale 与 Trace analysis reference 分型 | 已由证据定案 |
| ANA-12 | 扫描证据 IAC-022 与“Preview 和 Completed 边界”、SYN-03 | E1 progress/完成边界；E3 provisional overlay、last-good 与正式替换 | 已明确 |
| ANA-13 | 控制证据 Web snapshot/event、SYN-03 | E1 正式读数基于完整数据；E3 仅 Presentation decimation/min-max envelope | 已由证据定案 |
| ANA-14 | SEL-01/03 | E1/E2 Active/Selected 与 scoped number；E3 typed registry | 已由证据定案 |
| ANA-15 | DEL-01..05、OWN-01 | E2 cascade/最少对象不同；E3 四种删除 Command 与历史保留 | 待兼容目标 |
| MRK-01 | MRK-E01、SYN-03、OWN-01 | E1 Marker 归属 completed Measurement/Trace；E3 Definition/Evaluation 分离 | 已由证据定案 |
| MRK-02 | MRK-E02、SYN-03 | E1 stimulus/value 读数；E3 全分辨率 B/C 层输入 | 已由证据定案 |
| MRK-03 | MRK-E02 | E1 discrete/interpolated；E2 nearest tie、复数/标量插值和 gap | 待兼容目标 |
| MRK-04 | MRK-E02、Keysight Marker Object | E1/E2 reference/delta；E3 typed ID 引用与循环校验 | 已由证据定案 |
| MRK-05 | MRK-E03 | E2 Keysight Fixed Marker、非共同方言；E3 项目原生 immutable fixed value/revision | 已由证据定案 |
| MRK-06 | MRK-E02 | E1 max/min；E2 metric/tie；E4 算法黄金曲线 | 待算法/计量验证 |
| MRK-07 | MRK-E02 | E1 next/left/right；E2 起点、遍历、wrap | 待兼容目标 |
| MRK-08 | MRK-E02 | E1 target/transition；E2 polarity/interpolation；E4 算法 | 待算法/计量验证 |
| MRK-09 | MRK-E02 | E1/E2 threshold、excursion、plateau 边界 | 待兼容目标 |
| MRK-10 | MRK-E02 | E1/E2 tracking；E3 scheduler、last-good/stale | 已由证据定案 |
| MRK-11 | MRK-E02 | E1 bandwidth/composite search；E2 指标；E4 交点算法 | 待算法/计量验证 |
| MRK-12 | MRK-E04 | E1/E2 跨 Trace coupling 与 scope；E3 共享刺激意图、各 Trace 独立求值 | 已由证据定案 |
| MRK-13 | MRK-E02、OBJ-04/06 | E1/E2 marker-to sweep/scale/delay；E3 Channel/Trace/Placement 三种目标 Command | 已由证据定案 |
| MRK-14 | MRK-E05 | E1/E2 Marker Table/批量查询；E3 同代 publication pinning、CSV/分页 | 已由证据定案 |
| LIM-01 | LIM-E01/02、SYN-03 | E1 归属与 test/display 分离；E3 Definition/Evaluation/Placement presentation | 已由证据定案 |
| LIM-02 | LIM-E01 | E1/E2 upper/lower/single/off segments | 已由证据定案 |
| LIM-03 | LIM-E01/02 | E2 端点包含和插值方言差异 | 待兼容目标 |
| LIM-04 | LIM-E01、厂商 formatted-data flow | E1 格式化标量输入；E2 convert/reject；E4 单位/格式算法 | 待算法/计量验证 |
| LIM-05 | LIM-E03、采集质量证据 | E3 safety invariant：无效数据不得静默 Pass；产品已确认任一参与点无效，或者零参与点、空输入、没有任何有效参与数据时，核心总体为 Indeterminate；生产 Safety Policy 可汇总为 Fail 但不得映射 Pass 或改写原始结果 | 已明确 |
| LIM-06 | LIM-E03 | E1 总 Fail、失败点/刺激；E3 margin、失败区间、worst point 扩展 | 已由证据定案 |
| LIM-07 | LIM-E02/03、SCPI status 证据 | E2 lock/latch/aggregate；E3 revision-keyed aggregation | 待兼容目标 |
| LIM-08 | 控制证据 FIL-04 | E1/E2 Limit 文件/表；E3 独立 schema、version、staging validation | 已由证据定案 |
| LIM-09 | LIM-E01..03、统一 Web/SCPI Kernel | E3 consistency closure：单一 `LimitResultSnapshot` 服务 Web、SCPI 与状态入口；不是厂商内部复用事实 | 已由证据定案 |
| LIM-10 | LIM-E03 与高级分析证据 | 无三家共同 E1 下限；E3 独立 Pro evaluator，不污染基础 Segment | 待产品确认 |
| MATH-01 | MTH-01 | E1 Data→Memory；E2 容量/对象差异；E3 immutable Memory snapshot | 已由证据定案 |
| MATH-02 | MTH-02 | E1 复数 Data/Memory 四则；E2 快捷副作用；E3 typed node | 已由证据定案 |
| MATH-03 | MTH-03 | E2 Keysight interpolation 与厂商差异；E3 默认 `ExactAxis`、禁止静默按下标错配 | 待兼容目标 |
| MATH-04 | MTH-04 | E1 frozen/locked trace；E2 与 memory 生命周期差异；E3 分型 | 已由证据定案 |
| MATH-05 | MTH-05 | E1 smoothing；E2 stage/edge/aperture 差异 | 待兼容目标 |
| MATH-06 | MTH-06 | E1 min/max hold；E2 reset/format 差异 | 待兼容目标 |
| MATH-07 | MTH-07 | E1 range statistics；E2 format/endpoint 差异 | 待兼容目标 |
| MATH-08 | MTH-08 | 无三家共同 E1；E3 独立 ensemble accumulator；E4 容量 | 待产品确认 |
| MATH-09 | MTH-09 | E1 统计显示/查询；E2 字段差异；E3 同一 result snapshot | 已由证据定案 |
| MATH-10 | MTH-10 | E2 高端厂商能力；E3 有类型、无脚本、资源受限表达式图 | 待产品确认 |
| NET-01 | REF-01 | E1 Trace electrical delay；E2 media/符号；E3 per-Trace node；E4 黄金相位 | 待算法/计量验证 |
| NET-02 | REF-02 | E1 Port extension；E2 loss/order；E3 full-matrix node；E4 黄金网络 | 待算法/计量验证 |
| NET-03 | REF-03 | E1 网络 renormalization；E2 Z0/wave theory；E3 typed matrix node | 待产品确认 |
| NET-04 | REF-04 | E1 reference-plane 概念；E3 plane provenance chain | 已由证据定案 |
| NET-05 | FIX-01 | E1 fixture/Touchstone/orientation；E2 topology；E3 immutable revision | 已由证据定案 |
| NET-06 | FIX-02 | E1 embedding/de-embedding；E2 topology；E3 matrix algorithm；E4 黄金网络 | 待算法/计量验证 |
| NET-07 | FIX-03 | E2 condition threshold 未统一；E3 condition metric；E4 近奇异验证 | 待算法/计量验证 |
| NET-08 | FIX-03 | E1 阶段可区分；E2 order 不同；E3 typed graph + Profile | 待兼容目标 |
| NET-09 | MIX-01 | E1 mixed-mode 参数；E2 pair/Z0；E3 transform；E4 同代完整矩阵 | 待底软/硬件确认 |
| NET-10 | TDR-07、FIX-02 | E2 AFR/enhanced TDR/eye 为选件；E3 独立 extension；E4 硬件/计量 | 待产品确认 |
| TD-01 | TDR-01 | E1 transform 类型；E2 算法/选件；E3 typed node；E4 性能 | 待算法/计量验证 |
| TD-02 | TDR-02 | E1 harmonic grid/DC；E2 resample/extrapolate；E3 validator；E4 actual axis | 待算法/计量验证 |
| TD-03 | TDR-03 | E1 window；E2 family/preset；E3 精确参数；E4 浮点验证 | 待算法/计量验证 |
| TD-04 | TDR-04 | E1 range/resolution/distance；E2 zero-padding；E3 axis metadata；E4 容量 | 待算法/计量验证 |
| TD-05 | TDR-05 | E1 gate type/range/shape；E2 shape；E3 versioned definition | 待算法/计量验证 |
| TD-06 | TDR-06 | E1 inverse 后新频率响应；E2 normalization；E3 per-Trace gated snapshot；E4 黄金数据 | 待算法/计量验证 |
| TD-07 | TDR-07 | E1 全局依赖；E2 bad-point policy；E3 reject/impute quality；E4 噪声验证 | 待算法/计量验证 |

## 4. 对当前候选规则的结论

### 4.1 有充分外部行为证据的规则

1. **Channel 是采集设置主作用域。** 三家都把 stimulus、Sweep/Trigger/Average、校准或其主要部分放在 Channel，并让多个 Trace/Measurement 共享。
2. **Diagram/Window 是呈现容器。** 它负责布局、坐标、Trace 显示和交互，不应成为采集数据本身。
3. **Marker 和 Limit 跟随 Measurement/Trace。** 它们不是 Diagram 上无业务含义的装饰；Diagram 只呈现其符号、读数和线。
4. **Limit 的定义/测试与显示可见性需要分开。** Keysight/CMT 有直接独立命令，R&S 也公开可复用 Limit Line 与 check/display 的组合行为。
5. **扫频启动与完成必须分开。** `INIT`/trigger 后必须用具体完成机制同步，正式 Marker、Limit 和数据读取不能把“命令已接受”当“结果已完成”。
6. **对象 ID 必须有类型和作用域。** Keysight 明确存在 Measurement Name、Window Trace Number、Measurement Number 等不同标识；使用一个裸整数会产生真实兼容错误。

### 4.2 有方向性证据、但仍属于内部架构推导的规则

1. **Analysis Trace 与 Trace Placement 分层。** Keysight 的对象模型和三家的显示行为证明分析与显示分层有价值，但 R&S/CMT 未公开 Keysight 式 Measurement/Trace 二分。本项目因此采用 `AnalysisTrace(TraceSourceSpec)` 与 `TracePlacement`；Live Source 内含 `MeasurementSpec` 值对象，不再增加独立持久化 `MeasurementDefinition` 聚合。
2. **三层完成结果。** 厂商同步行为证明正式分析必须使用完整、一致的结果；项目把它实现为 A 层 `CompletedSweepBundle`、B 层 `CompletedMeasurementBundle` 和 C 层逐 Trace `AnalysisPublication`。不可变 Buffer、snapshot ID、revision、pinning 与失败隔离是 E3 实现选择。
3. **删除动作分层。** Keysight 对 display trace 与 Measurement 提供直接证据；R&S/CMT 没有形成相同的跨厂商 cascade 规则。本项目因此拆分 DeletePlacement、DeleteAnalysisTrace、DeleteDiagram 和 DeleteChannel，再由 Profile 组合，不宣称“删 Diagram 必然保留/删除 Trace”是行业统一事实。
4. **Marker/Limit 共用同一核心算法服务 Web/SCPI。** 外部一致性需要它，但厂商资料只证明两种控制面应呈现相同仪器状态，不公开其内部代码复用方式。

### 4.3 不能宣称为厂商共同事实的项目决策

1. **SCPI selection scope。** 三家公开资料更接近共享仪器 Active/Selected 状态，没有多连接隔离保证。项目规定 Web selection 始终 session-local；SCPI 由 Compatibility Profile 选择共享仪器、每 Channel 共享或连接局部语义。
2. **内部 ID 与历史。** 项目使用带类型的稳定 ID，并禁止把历史仍可引用的 ID 重新绑定到另一个语义对象；这是快照、审计和并发安全设计，不是厂商内部事实。
3. **删除政策。** 项目把 DeletePlacement、DeleteAnalysisTrace、DeleteDiagram、DeleteChannel 拆成不同 Command，默认引用安全，厂商命令由 Profile 映射为原子复合动作；这不是三家共同 cascade 语义。
4. **Continuous Operation 粒度。** 项目使用可取消的 ContinuousRun 父 Operation 和逐轮 SweepOperation；SCPI fence 只等待目标方言规定的工作集合。该层次是 E3 设计。

## 5. 建议进入逐项对齐的问题

1. SCPI 首选方言是否是项目原生、Keysight-like、R&S-like 或 CMT-like；每种方言的 selection scope 单独冻结。
2. 为选定 SCPI 方言冻结 DeletePlacement、DeleteAnalysisTrace、DeleteDiagram、DeleteChannel 的外部命令映射和“至少保留一个”规则。
3. 将 Limit 的 `definition enabled`、`test enabled`、`overlay visible`、`result latched` 拆成可测试状态，并为兼容方言定义组合命令副作用。
4. 将 `INIT accepted`、Sweep terminal、Average terminal、analysis publication 和 SCPI completion fence 分开；明确 Continuous 下每轮完成事件和 last-completed 查询规则。
5. 对 R&S ZNA/ZNB 目标固件的 Remote Control Manual 再补一次逐点 Limit report、Window deletion cascade 和多连接 selection 行为验证；本研究没有把未找到的内容当成“不支持”。

## 6. 一手资料索引

### Keysight

- [Channel Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Channel_Object.htm)
- [Measurement Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Measurement_Object.htm)
- [Trace Object](https://helpfiles.keysight.com/csg/N52xxA/Programming/COM_Reference/Objects/Trace_Object.htm)
- [Marker Object](https://helpfiles.keysight.com/csg/N52xxB/Programming/COM_Reference/Objects/Marker_Object.htm)
- [Referring to Traces, Measurements, Channels, and Windows Using SCPI](https://helpfiles.keysight.com/csg/N52xxA/Programming/Learning_about_GPIB/Referring_to_Traces_Measurements_Channels_Windows_Using_SCPI.htm)
- [Display SCPI](https://helpfiles.keysight.com/csg/NA520xA/Programming/GP-IB_Command_Finder/Display.htm)
- [Measure Limit SCPI](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Calculate/MeasureLIMit.htm)
- [Synchronizing the Analyzer and Controller](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)

### Rohde & Schwarz

- [ZNA 官方手册入口](https://www.rohde-schwarz.com/manual/ZNA)
- [R&S ZNA User Manual v39](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf)：§4.1.3 p.113、§4.1.3.3 p.115、§3.3.5.3 p.59、§4.2 p.138、§4.4.1 p.175。
- [R&S ZNB/ZNBT User Manual v72](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf)
- [Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html)
- [Hints and Tricks for Remote Control of Spectrum and Network Analyzers, 1EF62](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ef62/1EF62_1E.pdf)
- [R&S ZVL Operating Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_common_library/dl_manuals/dl_user_manual/ZVL_OperatingManual_en_09.pdf)（仅用于补充同系 R&S VNA 对 remote active trace 和复杂格式 Marker 搜索的明确文字，不替代 ZNA/ZNB 主证据。）

### Copper Mountain Technologies

- [Channel Allocation](https://coppermountaintech.com/help-cmtvna/1-port/channel-allocation.html)
- [Selection of Active Trace/Channel](https://coppermountaintech.com/help-cmtvna/1-port/selection-of-active-trace_channel.html)
- [Diagram](https://coppermountaintech.com/help-cmtvna/1-port/diagram.html)
- [CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html)
- [CALC:PAR:SEL](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcparsel.html)
- [CALC:MARK](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calcmark.html)
- [CALC:LIM:DATA](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calclimdata.html)
- [Display command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/display.html)
- [TRIG:SING](https://coppermountaintech.com/help-r/trigsing.html)
- [`*OPC?`](https://coppermountaintech.com/help-cmtvna/Programming-Manual/opc_question.html)
