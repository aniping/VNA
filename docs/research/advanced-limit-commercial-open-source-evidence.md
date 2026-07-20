# 高级 Limit：商用与开源 VNA 实现证据

## 1. 研究范围与证据边界

访问日期：**2026-07-20**。

本文回答 `LIM-10` 涉及的四类能力在商用与开源 VNA 中如何出现：

1. 普通 upper/lower segmented Limit；
2. Ripple 与 Flatness；
3. 二维 mask/envelope；
4. 连续 N 次或其他跨 Sweep 生产判定。

商用品只采用厂商官方用户手册、编程手册和产品资料。它们能够证明外部对象、配置表、算法定义、SCPI 命令和可观察结果，**不能证明厂商内部使用了哪些类、线程或模块**。开源项目则固定到具体 commit，可直接说明源码组织；“未找到”仅表示在所审计 commit 的跟踪源码与官方文档中没有发现，不表示所有分支或未来版本永远不支持。

本文中的“二维”必须区分三件事：

- 普通 Limit 的 upper/lower 折线已经在直角坐标系 `(stimulus, response)` 中形成允许包络；
- Smith/Polar 图上的复数平面区域是另一种几何判定，例如圆形区域；
- Eye Mask 是时域眼图模板，不能因为也叫 mask 就塞进普通 VNA Limit Segment。本轮资料没有证明它属于三家基础 VNA Limit 的共同能力。

## 2. 先给结论

- 三家商用实现都把普通上下限作为“按 Trace/Measurement 配置的分段表 + 单次测量结果判定”，而不是 Diagram 像素碰撞。
- Ripple 是独立指标：在频段内求 `max(response) - min(response)` 再与阈值比较。Keysight、R&S、CMT 都公开了独立的 Ripple 配置语义；它是成熟常用能力，但“是否放 Pro 版”属于产品分级，不能由技术证据决定。
- Flatness 不等同于 Ripple。Keysight、CMT 和 LibreVNA 的 Flatness 都表现为 Marker/分析指标；典型定义是相对两端点连线的最大正、负偏差之和，而不是简单峰峰值。
- 通用“任意二维 Mask”不是三家共同基础能力。R&S 明确提供复数图上的 circle limit；Keysight 的普通 Limit 明确不用于 Smith/Polar。因而应先实现具体几何 evaluator，不应先造一个含义模糊的万能 Mask。
- 本轮没有在基础 Limit 功能中找到“连续 N 次失败才 Fail”的官方内置语义。Keysight Handler 与 CMT Manufacturing Plug-in 表明，单次 Sweep/一次 DUT 的结果先产生，跨次筛选、锁存、分 bin 和质量流程位于生产自动化层。输出电平锁存或 Sweep averaging 也不等于“连续 N 次失败”。
- 开源实现明显较轻：LibreVNA 把 Limit 线和求值放在 XY Plot 内；NanoVNA-QT 的 “Graph limits” 只是坐标轴范围；NanoVNA-Saver 有若干专用分析，却没有通用 Limit/Ripple/Mask/跨 Sweep 判定框架。它们适合作为算法或交互参考，不足以作为成熟产品的整体架构模板。

## 3. 商用 VNA 的公开实现

| 厂商 | 普通 Limit | 独立分析/几何判定 | 结果聚合与生产层 | 本轮未证实的能力 |
| --- | --- | --- | --- | --- |
| Keysight | 每条 Measurement Trace 的 `MIN/MAX` 分段表，测试与显示分离 | 独立 Ripple Limit；Flatness 属 Marker Math | Channel Global Pass/Fail 聚合多个已启用测试，Handler I/O 输出一次触发的结果 | 基础 Limit 内置“连续 N 次失败” |
| R&S | Limit Check 功能族中的 upper/lower 分段折线 | 同一功能族另有 ripple 与复数平面 circle | 单次 Limit Check 结果可驱动 USER PORT TTL | 把 circle 扩展为任意 polygon 或时域 Eye Mask；基础 Limit 内置“连续 N 次失败” |
| CMT | 独立普通 Limit 命令族 | Ripple Limit、Peak Limit 各有独立命令族；Flatness 属 Marker Math | Manufacturing Test Plug-in 承担规格、结果和生产流程 | 基础 Limit 内置“连续 N 次失败” |

### 3.1 Keysight

普通 Limit 以每条 Measurement Trace 的 `MIN/MAX` 分段表表示；每段含起止 stimulus 和起止 response，测试开关与显示开关分离，失败点可逐点查询。官方还明确说明普通 Limit 不适用于 Smith/Polar 格式，且点密度不足可能漏掉折线之间的越界。[Using Limit Lines](https://helpfiles.keysight.com/csg/pxivna/S4_Collect/Use_Limits_to_Test_Devices.htm)；[CALCulate:MEASure:LIMit](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Calculate/MeasureLIMit.htm)

Ripple 不是普通 Segment 的一个 type，而是可独立启停的 Ripple Limit Test：每条 Trace 可定义若干频段，算法只使用实际测量点，在频段内求最大值与最小值之差；各频段分别判断，结果再进入 Channel 的 Global Pass/Fail。[Keysight Ripple Limit Test](https://helpfiles.keysight.com/csg/e5080b/S4_Collect/Use_Ripple_Limit_Test.htm)

Flatness 又是另一类 Marker Math。Keysight E5061B 将其定义为：相对 Marker 1、2 连线，线上方最大偏差与下方最大偏差之和。[Obtaining Span, Gain, Slope, and Flatness between Markers](https://helpfiles.keysight.com/csg/e5061b/measurement/data_analysis/obtaining_span_gain_slope_and_flatness_between_markers.htm)

Global Pass/Fail 负责将多个已启用测试聚合；Handler I/O 在一次触发所需测量及计算完成后输出 Pass/Fail 和 strobe，并可选择 Channel/Global scope。文档没有提供“连续 N 次失败”参数；这里的 PASS/FAIL default mode 是输出时序与电平策略，不是跨 Sweep 去抖器。[Keysight Handler I/O](https://helpfiles.keysight.com/csg/e5080b/Programming/RPHandler_IO_Connector_%28E5080A%29.htm)

**可确认边界：** Keysight 公开了普通 Limit、Ripple、Flatness、全局聚合和生产 I/O 的不同外部接口；不能据此断言其内部恰好对应五个 C++ 类。

### 3.2 Rohde & Schwarz

R&S ZNA/ZNB 的 Limit Check 功能族公开四种不同定义：upper、lower、ripple 和 circle。upper/lower 由分段折线组成；允许重叠和 gap，重叠处使用更严格条件（逻辑 AND），并只在实际测量点检查。Ripple 在频段内限制最大、最小 response 的差。Circle 由复数图上的中心和半径定义，是专门的几何区域，而不是任意 polygon mask；Limit 违反还可驱动 USER PORT TTL 输出。[R&S ZNA User Manual v39，§4.4.1，pp.175–183](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf)；[R&S ZNB3000 User Manual v04，§4.4.1](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1179_7491_01/ZNB3000_UserManual_en_04.pdf)

截至访问日，R&S 官方入口列出的当前 ZNA 手册为 v40（2026-03-27）；上面的页码固定引用项目既有研究基线 v39，避免版本漂移。[R&S ZNA User Manual 官方入口](https://www.rohde-schwarz.com/us/manual/rs-zna-user-manual_78701-601863.html)

**可确认边界：** R&S 在同一 Limit Check 功能族中区分多种 evaluator 语义；公开资料不证明它们共享或不共享某个内部基类，也没有把 circle 扩大为任意二维/眼图 Mask。

### 3.3 Copper Mountain Technologies

CMT 的普通 Limit、Ripple Limit 和 Peak Limit 是三个独立的 Trace 参数及 SCPI 命令族。普通 `CALC:LIM:DATA` 使用 `Off/Upper/Lower/Single Point` 分段，最多 100 段；Ripple 使用独立频段表和 `CALC:RLIM...` 命令；Peak Limit 再按峰值极性以及 stimulus/response 范围判断。[CMT Limit Test](https://coppermountaintech.com/help-cmtvna/1-port/limit-test.html)；[CMT Ripple Limit Test](https://coppermountaintech.com/help-cmtvna/1-port/ripple-limit-test.html)；[CMT Peak Limit Test](https://coppermountaintech.com/help-cmtvna/1-port/peak-limit-test.html)；[CMT CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html)

CMT 把 Flatness 放在 `CALC:MARK:MATH:FLAT...` Marker Math 下，而不是普通 Limit/Ripple 表中。其 Manufacturing Test Plug-in 再负责跨工作站的规格文件、结果管理和可定制生产流程，这支持“单次分析结果”和“生产质量策略”分层，但没有公开连续 N 次的固定算法。[CMT CALCulate command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html)；[CMT Manufacturing Test Plug-in](https://coppermountaintech.com/vna/manufacturing-test-plug-in/)

## 4. 开源 VNA 的真实源码

### 4.1 LibreVNA

审计版本：[`20247625ad5269c8cdf2fb4408be3666e8679041`](https://github.com/jankae/LibreVNA/tree/20247625ad5269c8cdf2fb4408be3666e8679041)。

LibreVNA 用户手册将 Limit 明确限定为 XY Plot 的 custom limit line；每条线可选 `Dont Care / High Limit / Low Limit`，Polar Chart 不支持 custom constant line。[官方手册源码](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Documentation/UserManual/manual.tex#L1124-L1154)

源码可确认的组织如下：

- `XYPlotConstantLine` 自身保存 axis、pass/fail 类型和折线点。[tracexyplot.h](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/Traces/tracexyplot.h#L10-L54)
- `pass()` 在折线范围外直接通过，在相邻点间线性插值后比较 high/low；NaN/Inf 是否通过由全局 `limitNaNpasses` 偏好决定。[tracexyplot.cpp](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/Traces/tracexyplot.cpp#L1347-L1392)
- 求值发生在 `TraceXYPlot::draw()` 内：绘制每条可见 Trace 时逐点遍历全部 constant lines，并写入一个布尔 `limitPassing`。[tracexyplot.cpp](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/Traces/tracexyplot.cpp#L428-L578)
- Tile 树用逻辑 AND 汇总，空 Tile 直接通过；SCPI `ACQuire:LIMit?` 最终只返回 `PASS/FAIL`。[tilewidget.cpp](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/CustomWidgets/tilewidget.cpp#L126-L136)；[vna.cpp](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/VNA/vna.cpp#L1607-L1613)
- Flatness 是独立 Marker 类型：以两个 helper marker 的连线为基准，扫描区间数据，记录最大正、负偏差；它没有 Limit 阈值和 Pass/Fail 状态。[marker.cpp](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/Traces/Marker/marker.cpp#L2001-L2044)

截至该 commit，在非第三方 GUI 源码与用户手册中未找到独立 Ripple Limit 表、circle/polygon mask 或连续 N 次判定。这个结构简单可读，但把定义、求值和呈现耦合进 Plot，且使用二态与可配置 NaN 通过策略，不适合作为本项目生产级领域内核的直接模板。

### 4.2 NanoVNA-QT

审计版本：[`0aa6ee4e68ade0285755f06c2eab240e2d0beea1`](https://github.com/nanovna/NanoVNA-QT/tree/0aa6ee4e68ade0285755f06c2eab240e2d0beea1)。源码里的 `Graph limits` 只编辑 magnitude 图的 Y 轴 minimum、maximum 和 division 数，并由 `setRange/setTickCount` 应用；它不是规格 Limit 或 Pass/Fail evaluator。[mainwindow.C](https://github.com/nanovna/NanoVNA-QT/blob/0aa6ee4e68ade0285755f06c2eab240e2d0beea1/vna_qt/mainwindow.C#L960-L973)；[networkview.C](https://github.com/nanovna/NanoVNA-QT/blob/0aa6ee4e68ade0285755f06c2eab240e2d0beea1/vna_qt/networkview.C#L299-L307)

截至该 commit 的 116 个跟踪文件中，未找到 Ripple、Flatness、Pass/Fail 或测试 Mask evaluator。它只能证明轻量显示软件的能力边界，不能为 LIM-10 提供正向架构范例。

### 4.3 NanoVNA-Saver

审计版本：[`3445a0ab86161f9c886c9d6f215eb57cab9b6f45`](https://github.com/NanoVNA-Saver/nanovna-saver/tree/3445a0ab86161f9c886c9d6f215eb57cab9b6f45)。官方功能清单包含分段采集、Marker、TDR 和滤波器分析，但未列通用 Limit/Ripple/Mask。[README.rst](https://github.com/NanoVNA-Saver/nanovna-saver/blob/3445a0ab86161f9c886c9d6f215eb57cab9b6f45/README.rst#L46-L66)

源码按具体分析类型组织。例如 `VSWRAnalysis` 使用一个 threshold 查找低于阈值的 VSWR 区域并显示起止/最小值，没有生成通用 Pass/Fail 结果；`BandPassAnalysis` 计算峰值、带宽、Q 和 roll-off，也不是 Limit evaluator。[VSWRAnalysis.py](https://github.com/NanoVNA-Saver/nanovna-saver/blob/3445a0ab86161f9c886c9d6f215eb57cab9b6f45/src/NanoVNASaver/Analysis/VSWRAnalysis.py#L35-L101)；[BandPassAnalysis.py](https://github.com/NanoVNA-Saver/nanovna-saver/blob/3445a0ab86161f9c886c9d6f215eb57cab9b6f45/src/NanoVNASaver/Analysis/BandPassAnalysis.py#L35-L155)

截至该 commit，在 `src/tests/docs` 中未找到通用 upper/lower Limit、Ripple Limit、二维 Mask 或跨 Sweep 连续失败计数。

## 5. 对本项目架构的证据化结论

商用品的外部行为与开源源码共同支持以下分层，但这是**本项目的架构推论**，不是对厂商内部代码的声称：

```text
单次 AnalysisPublication
  -> SegmentedEnvelopeEvaluator（普通 upper/lower）
  -> RippleEvaluator（频段 max-min）
  -> MetricEvaluator（Flatness、Peak、Bandwidth 等）
  -> GeometricRegionEvaluator（可选 circle；不是万能 Mask）
  -> 每个 evaluator 产生不可变的单次结果与诊断明细
  -> Trace/Channel/Sweep Result Aggregator
  -> ProductionQualificationPolicy（连续 N 次、锁存、bin、QMS）
  -> Web / SCPI / Handler 仅适配同一结果
```

据此，`LIM-10` 不应把 Ripple、Flatness、circle/mask 和连续 N 次都塞进 `LimitSegment`，但也不应直接把它们全部归为 Pro：

- **Core 基础能力：** 普通 upper/lower segmented envelope、单次三态结果、失败点/原因、统一聚合接口；
- **建议作为常规分析能力评估：** Ripple。三家均公开支持，技术上应是独立 evaluator；是否 Core/Pro 是产品范围问题；
- **Marker/Metric 能力：** Flatness 先作为可查询指标；需要规格判定时由 metric-threshold evaluator 消费，不与 Ripple 共用错误定义；
- **可选高级能力：** 复数平面 circle 等明确几何 primitive；任意 polygon 或 Eye Mask 必须由真实用例驱动，不能以“Mask”一个词预留万能对象；
- **Manufacturing/Pro 候选：** 连续 N 次、锁存、批次、bin 和 QMS 属跨 Sweep 有状态策略，消费单次原始结果但不得改写它。

最关键的成熟产品差别不是功能名称多，而是把“单次测量事实”“分析算法”“结果聚合”“生产决策”和“显示/协议”分开。商用资料支持这种责任边界；LibreVNA 则反向展示了把求值放进 Plot 后的简化与局限。

实现上，各 evaluator 应是只消费一份不可变 `AnalysisPublication` 的无状态计算单元，并把单次结果交给共享 Aggregator；`ProductionQualificationPolicy` 才保存跨 Sweep 的连续计数、锁存或分 bin 状态。Core/Pro 授权与这条数据流正交：授权只决定某种 evaluator 或生产策略能否创建/启用，不应让同一算法在不同版本中改变输入、结果语义或聚合规则。
