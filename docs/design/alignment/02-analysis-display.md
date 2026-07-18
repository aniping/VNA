# 分册 02：分析与显示逐项对齐

> 状态：已逐项完成官方证据归类；算法/计量、兼容目标和少量产品范围仍待闭合。

本分册沿用总矩阵的七种状态。对象/所有权结论按 [对象与分析一手证据](../../research/official-vna-object-and-analysis-evidence.md) 归类，校准、Math、参考面、夹具、混模和时域按 [校准与处理链一手证据](../../research/official-vna-calibration-processing-evidence.md) 归类；`已由证据定案` 不表示厂商内部实现相同，而表示项目已承担并闭合该架构选择。

## Measurement、Trace 与 Diagram

| ID | 功能项 | 商用级外部行为 | 当前建议 | 等级 | 架构落点 | 数据阶段/重算语义 | 关键异常/边界 | 验收证据 | 状态 |
|---|---|---|---|---|---|---|---|---|---|
| ANA-01 | Analysis Trace 归一化 | 三家均区分可分析结果与 Diagram/Window 呈现，但只有 Keysight 公开独立 Measurement；R&S/CMT 外部 Trace 是复合概念。 | 使用 `AnalysisTrace(TraceSourceSpec)`；Live Source 内含 MeasurementSpec 值对象，不建立独立 MeasurementDefinition 聚合。 | Core | Channel / Instrument Kernel | Live 输入引用 B 层 `CompletedMeasurementBundle`；Frozen/Memory、Imported、Derived 输入分别冻结静态数据、导入数据或一个/多个上游 C 层 publication，Trace 身份不按规格去重。 | 修改 Source Spec 时重新验证 Marker/Limit/Memory/correction；校准辅助观测不伪造 Trace；Derived 图禁止循环。 | Keysight Measurement 与 R&S/CMT Trace 映射契约；同 S21 多 Trace 共用一次采集；四种 Source Ref provenance 测试。 | 已由证据定案 |
| ANA-02 | 稳定 Analysis Trace 身份 | Marker、Limit 和协议针对一个可分析目标；厂商称 Measurement 或 Trace，均不能由窗口像素代替。 | AnalysisTrace 使用带类型的稳定 ID/revision，不由 Diagram 所有；外部编号按方言作用域映射。 | Core | Channel / Instrument Kernel | 配置修改形成新 Trace revision；历史快照不改写。 | 内部 ID 不重新绑定；被 Math/coupling 引用时删除需拒绝或显式级联。 | 删除/重建 Placement 后 Marker/Limit 仍可查询；编号作用域混淆回归。 | 已由证据定案 |
| ANA-03 | Trace 求值快照 | 一次读数必须对应已经完成的一致输入和确切分析设置；厂商未公开内部快照结构。 | Control Executor 派发前原子取得全部 typed refs 的 `PinnedInputSet` 与 output reservation；Pipeline 只返回持有 `CandidateCommitLease` 的 `PublicationCandidateBatch`。Trace/Marker/Limit 在私有 batch 验证 `ResultClosure` 后，由一个 `DomainCommitBundle` 原子发布 C、Operation/fence、Event 与 retention delta。只有匹配 current-input token 的后台 Live candidate 携带 CAS `TraceAnalysisHead` patch；历史 B/Stage exact query 使用 `HeadPromotionPolicy::None`，只发布/返回 C。Marker Invalid/Incomplete 与 Limit Indeterminate 是成功领域结果；内部 evaluator/commit 失败才使新 C 整批不可见。不可中断节点把 input/output/budget 一并转 Drain。 | Core | Measurement Pipeline / Measurement Data Store / SnapshotCatalog | `AnalysisInputRefSet + AnalysisTraceRevision + PinnedInputSet + ExecutionContext → EvaluateTraceOperation → PublicationCandidateBatch → AnalysisPublication`；Live B、MeasurementStage、Frozen/Imported/Derived 及 Memory/Accumulator supplemental refs 均有类型；candidate 从 worker return 到 commit/abort 始终持 lease。 | 输入回收、多输入只 pin 半套、candidate-to-commit 所有权缺口、代次不一致、历史 query 倒退 Head、领域 invalid 被误当系统失败、单条 child 内部失败、定义中途改变、超时 worker 仍运行；不得暴露半套 C、拖垮父结果或提前释放预算。 | 全 typed input/revision/quality 可反查；retention 竞态；stop/deadline/Drain input pin；candidate commit/abort lease；Live/Stage/Derived generation；新 Live C 可 CAS 提升而历史 exact C 不改变 Head；Marker Invalid/Limit Indeterminate 成功发布；`DomainCommitBundle` 故障不见半套 C/Head/Event。 | 已由证据定案 |
| ANA-04 | 无需重扫的重算 | 商用品在 Hold/Memory 上可继续修改 format、Math、Marker、Limit；这些是分析行为而非 RF 扫描。 | 采集 Operation 与 EvaluateTraceOperation 正交。 | Core | Analysis Scheduler | 对 last-good 正式快照派生；Preview 不参与。 | 连续 Sweep 中过期求值合并，只发布兼容代次。 | Hold 下改格式/Limit，无 Board 调用且结果更新。 | 已由证据定案 |
| ANA-05 | Trace Placement | Window/Diagram 对可分析 Trace 提供显示绑定；Keysight 明确可 move/feed，R&S/CMT 的分配粒度不同。 | Placement 只保存 Diagram 引用、颜色、线型、可见性、轴、scale 和 z-order；Product/Profile 限制一条 Trace 的 Placement 数量。 | Core | DisplayWorkspace | 只改变 Workspace revision，不触发 RF/分析重算。 | DeletePlacement、隐藏、换轴不删除 AnalysisTrace；兼容命令可组合删除。 | 无 Placement 仍可查询；多 Placement Profile 下独立缩放/样式。 | 已由证据定案 |
| ANA-06 | Diagram 对象 | 三家均把 Window/Diagram 作为显示容器，而非采集或判定对象。 | 核心统一称 Diagram，前端 Chart 名称不进入领域；只拥有 Placement 和 Overlay。 | Core | DisplayWorkspace / Diagram | Diagram 消费 TracePlacement 和 AnalysisPublication。 | 核心删除要求显式选择 empty/move/delete traces；厂商副作用由 Profile 映射。 | 多图布局保存恢复；四类删除 Command 的所有权测试。 | 已由证据定案 |
| ANA-07 | 多 Diagram 布局 | 商用品支持单图、多图、网格、最大化及活动图；active 的共享作用域随控制面/厂商而异。 | Page/Workspace 管持久布局；Web active diagram/zoom/pan 属 ClientSession，SCPI active Diagram 由 Compatibility Profile 决定。 | Core | DisplayWorkspace + ClientSession | 临时 zoom/pan 会话级；显式保存才形成 Workspace revision。 | 两浏览器互不影响；SCPI 共享/局部选择均按 Profile 测试。 | 两浏览器独立选择/缩放；厂商 Profile active Diagram 回归；布局 round-trip。 | 已由证据定案 |
| ANA-08 | 多 Trace 叠加 | 一个图可叠加多个 Trace；R&S 明确允许跨 Channel，CMT 受 Channel window 约束。 | 核心建立 coordinate/X-domain/Y-scale compatibility matrix 并支持跨 Channel；Compatibility Profile 可收紧布局。 | Core | Diagram Validator | 可绘制网格的兼容条件可宽于共享 Marker/Limit。 | 不同单位、不同 Channel、不同 sweep generation、Smith+Cartesian。 | 合法组合显示，非法组合返回结构化原因；CMT-style 限制 Profile。 | 已由证据定案 |
| ANA-09 | Cartesian 格式 | 商用品至少提供幅度、相位、实虚部、SWR、群时延 | Core 冻结为 Complex/Real/Imag、LinMag/LogMag、Phase/Unwrapped、GroupDelay、SWR；每种 projection 声明输入与单位 | Core | AnalysisProjection nodes | 格式化通常由 corrected/processed complex 输入；群时延邻点依赖 | log(0)、phase gap、非单调轴、端点群时延 | 与黄金复数数组逐格式比对 | 待算法/计量验证 |
| ANA-10 | Smith/Polar | 反射/网络复数结果可按 Smith 阻抗/导纳或 Polar 显示和读值 | 将 coordinate 与 marker readout projection 分开 | Core | Diagram Coordinate + TraceProjection | 保留复数输入，不能只存屏幕 X/Y | 非反射参数、Z0 变化、复数 Z0、无效点 | 已知阻抗/反射系数的 Smith/Polar 黄金点 | 待算法/计量验证 |
| ANA-11 | Scale/Reference | scale/div、reference value/position、auto scale 行为稳定 | 视觉 scale 属 Placement；影响分析的 reference/electrical delay 另建节点 | Core | TracePlacement / Command | 视觉修改不重算数据；auto scale 读取全分辨率或明确采样策略 | 空数据、NaN、全常数、极端 dynamic range | auto scale 边界与保存恢复测试 | 已由证据定案 |
| ANA-12 | Preview 视觉状态 | 用户能区分渐进结果、last-good 和新正式结果 | provisional overlay + sweep progress；`DiagramFrameRefSet` 为每个 Placement 固定确切 C。普通视觉 latest-per-placement 明示 generation/stale；coupling/共享 Limit/Math 满足同步政策后原子切帧 | Core | Web View Model + Diagram selector | Preview 只读 `ChunkReadView` 或独立 `PreviewTile`，运行 streaming-safe 子图且不能提升为正式求值；Marker/Limit 始终来自 FrameRefSet 指向的正式 C | 非 streaming 节点显示 last-good/unavailable；多 Placement 不同代、旧 preview 迟到、失败/取消、同步政策不满足 | 扫描中/失败/取消/完成；多代 overlay 标签；coupled 原子切帧；Preview 饱和/迟到不污染正式 C | 已明确 |
| ANA-13 | 绘图抽稀 | 大点数曲线流畅，但读数、判定、导出保持全精度 | 抽稀只在 Web Presentation Adapter | Core | Web Plot Projector | Marker/Limit/Statistics/SCPI 始终使用全分辨率快照 | 极窄峰不能因像素抽稀在视觉上完全消失，需 min/max envelope 策略 | 构造单点尖峰，显示可见且判定精确 | 已由证据定案 |
| ANA-14 | 对象选择与编号 | 官方资料明确 Channel、Measurement/Trace、Window/Diagram、Window Trace 和 Marker 编号具有不同作用域。 | 内部带类型稳定 ID；名称可重复；SCPI index 由 Dialect + documented scope 映射，不默认 session-local。 | Core | Identity Registry + Selection Context | rename 不改变引用；删除后索引行为由方言固定。 | 重复名、索引洞、对象重排、持久化恢复、Measurement number 与 Window Trace number 混淆。 | Web 重命名后 SCPI 仍引用正确对象；三方言编号作用域测试。 | 已由证据定案 |
| ANA-15 | 所有权与删除 | 厂商均有独立删除动作，但 display Trace、analysis Trace、Diagram、Channel 的 cascade 与最少保留数量明显不同。 | 核心拆分 DeletePlacement/DeleteAnalysisTrace/DeleteDiagram/DeleteChannel，默认引用安全；Compatibility Profile 组合映射。 | Core | Instrument Kernel | 历史快照 tombstone/retention，不随当前对象删除。 | 活动 Query/Operation、Math 循环、CalSet 引用、最后一个 Trace/Diagram。 | canonical 删除矩阵；选定方言的删除副作用回归；历史可读性。 | 待兼容目标 |

## Marker

| ID | 功能项 | 商用级外部行为 | 当前建议 | 等级 | 架构落点 | 数据阶段/重算语义 | 关键异常/边界 | 验收证据 | 状态 |
|---|---|---|---|---|---|---|---|---|---|
| MRK-01 | Marker 定义、归属与结果 | 官方行为表明 Marker 面向 Keysight Measurement 或 R&S/CMT Trace，而非 Diagram；设置与某次结果上的读值分开。 | MarkerDefinition 属 AnalysisTrace；MarkerEvaluationSnapshot 独立；协议 Adapter 负责 Measurement/Trace 寻址映射。 | Core | AnalysisTrace / AnalysisPublication | 绑定 trace、input node、projection、snapshot 和 revisions。 | 输入过期显示 Stale，不把旧值当新扫描结果；隐藏 Placement 不删除 Marker。 | 三厂商寻址映射；修改 Marker 后 last-good 重算且 provenance 完整。 | 已由证据定案 |
| MRK-02 | Normal Marker | 指定刺激位置读取响应和实际落点 | 支持 On/Off、requested X、actual X/Y、单位和状态 | Core | MarkerEvaluator | 只读正式全分辨率 Trace | 超范围、无数据、无效点、轴非单调 | 点上/点间/越界/invalid 黄金测试 | 已由证据定案 |
| MRK-03 | Discrete/Interpolated | 可选择吸附采样点或插值读值，规则一致 | Profile 固定 nearest tie、复数/标量插值和 segment 边界 | Core | Marker Projection | 插值作用于声明的数据投影，禁止默认跨 gap | 等距两点、相位 wrap、log X、重复频点 | 各轴/格式插值黄金表 | 待兼容目标 |
| MRK-04 | Reference/Delta | Delta 明确引用参考 Marker 并返回 ΔX/ΔY | Reference Marker 独立定义；Delta 保存引用 ID | Core | MarkerSet | 两者必须绑定兼容 Trace/X-domain | 引用删除、跨 Trace、单位不兼容、引用循环 | 删除/改参考及跨图 coupling 测试 | 已由证据定案 |
| MRK-05 | Fixed Marker | 固定 Marker 保持用户设置的参考值，不随 Sweep 数据移动 | Fixed kind 与普通 Marker 分开 | Core | MarkerDefinition | 是否仍显示 live delta 由 projection 定义 | format/unit 改变、状态恢复 | 固定值与实时 Trace 独立性测试 | 已由证据定案 |
| MRK-06 | Max/Min Search | 在声明范围和 metric 中找到确定性极值 | SearchSpec 声明 Max/Min、metric、domain 和 tie-break | Core | MarkerSearch | 基于全分辨率正式标量/显式复数 metric | 相等峰、端点包含、invalid gap、非单调段 | 合成多峰含等峰的确定性测试 | 待算法/计量验证 |
| MRK-07 | Next Left/Right | 从当前 Marker 沿确定的遍历方向寻找下一个峰/谷 | Profile 固定起点、端点、segment 顺序与 wrap 行为 | Core | MarkerSearch | 新结果形成 Marker evaluation，不直接改数组 | 反向 Segment、重复 X、无下一峰 | 多 Segment 遍历黄金序列 | 待兼容目标 |
| MRK-08 | Target/Transition | 搜索与目标值相交或满足上/下降沿的点 | 声明 target、polarity、transition、range 和 interpolation | Core | MarkerSearch | 消费有序标量 projection | 多交点、切触不穿越、无交点、无效区 | 合成交点/切点/缺口测试 | 待算法/计量验证 |
| MRK-09 | Peak 条件 | peak 搜索可设 polarity、threshold、excursion，避免噪声峰 | 条件进入版本化 SearchSpec | Core | MarkerSearch | 每次正式快照重新求值 | plateau、阈值相等、端点峰、噪声 | 与目标兼容仪器边界用例对比 | 待兼容目标 |
| MRK-10 | Tracking Marker | 每个新正式 Sweep 后重新搜索，而不是沿用旧 index | Tracking 订阅 compatible AnalysisPublication；可暂停 | Core | Analysis Scheduler | 失败/取消保持 last-good 并标 Stale | 扫描快于搜索、配置 revision 切换、hold | 多 Sweep 峰漂移与任务合并测试 | 已由证据定案 |
| MRK-11 | Bandwidth/Filter Search | 返回左右交点、带宽、中心、Q、loss/notch 等完整复合结果 | 结构化结果含 Complete/Incomplete/Invalid | Core | Composite Marker Analysis | 输入 metric、reference level 和 search domain 显式 | 缺一侧交点、多个通带、平顶、invalid gap | 带通/带阻黄金曲线与缺交点测试 | 待算法/计量验证 |
| MRK-12 | Marker Coupling | 多 Trace Marker 可共享 X，但只在轴语义兼容时耦合 | Coupling group 只共享刺激意图，各 Trace 独立求实际点/值 | Core | MarkerCoupling | 不要求同一 Y 格式；要求兼容 X-domain 和 interpolation policy | 不同网格、不同 Channel 代次、循环 group | 多图多轴 coupling 测试 | 已由证据定案 |
| MRK-13 | Marker-to 操作 | Marker 可设置 start/stop/center/span/ref level/electrical delay | 每个操作转换为正常 Command：start/stop/center/span 目标 Channel，electrical delay 目标 AnalysisTrace，ref level/position 必须目标 TracePlacement；方言未显式给 Placement 时由 Selection Context 唯一解析，歧义则报错 | Core | Instrument Kernel Commands | Channel 操作可能触发下一 Sweep；Trace/Placement 操作基于 last-good 重算或仅改呈现，不在 MarkerEvaluator 中直接写状态 | 多 Placement 歧义、span 非法、活动校准、并发修改、RF 安全策略 | Web/SCPI marker-to 同源、目标作用域和歧义测试 | 已由证据定案 |
| MRK-14 | Marker Table/导出 | 官方已有 Marker Table 与全部 Marker stimulus/response 批量读回；CSV/批量导出是项目扩展 | Core 提供同代表格和批量 Query；CSV/高级全峰导出列 Pro/E3 | Core/Pro | Analysis Query / Export | 表中所有行绑定同一 analysis publication | 某 Marker invalid/incomplete，不能用 0 替代 | 表格、SCPI query 和 CSV 同代对比 | 已由证据定案 |

## Limit Line 与判定

| ID | 功能项 | 商用级外部行为 | 当前建议 | 等级 | 架构落点 | 数据阶段/重算语义 | 关键异常/边界 | 验收证据 | 状态 |
|---|---|---|---|---|---|---|---|---|---|
| LIM-01 | 定义/测试/显示/结果分离 | 三家官方行为均表明 Limit 面向 Measurement/Trace，且测试开关和显示开关可分；它不是 Diagram 上的一条装饰线。 | LimitDefinition 属 AnalysisTrace；test enabled、overlay visible、result snapshot 分开，协议 Adapter 映射组合副作用。 | Core | AnalysisTrace / Diagram / AnalysisPublication | 定义变化基于 last-good 立即重算；显示变化不重算判定。 | 隐藏线仍可测试；关闭测试不残留新 Fail；删除 Placement 不删定义。 | 三厂商寻址映射；显示/测试四组合；R&S 组合动作回归。 | 已由证据定案 |
| LIM-02 | Upper/Lower Segment | 支持水平、斜线、单点、off/gap 和多段上下限 | 有序 Segment 表，端点和插值规则版本化 | Core | LimitSet | 消费已格式化、单位明确的全分辨率标量 Trace | 重叠段、交叉上下限、零长度段、未排序输入 | 表格校验和逐点黄金判定 | 已由证据定案 |
| LIM-03 | 端点与插值 | 边界等于 Limit 是否通过、段端点是否包含必须固定 | 由 CompatibilityProfile 明确 inclusive/exclusive 和 X interpolation | Core | LimitEvaluator | 使用实际刺激轴，不按像素线判断 | 重复 X、Segment 边界、log X、gap | 边界 ±1 ULP 与目标方言兼容测试 | 待兼容目标 |
| LIM-04 | 格式/单位绑定 | 改 Trace format 后不能继续用旧 dB/degree 限值静默判断 | 兼容时显式转换，否则 Reject/Mismatch | Core | Trace/Limit Validator | Limit 保存 input projection 和 unit revision | LogMag↔LinMag、phase wrap、SWR、Smith/Polar | 格式切换后的 convert/reject 状态测试 | 待算法/计量验证 |
| LIM-05 | Pass/Fail/Indeterminate | 无效输入不得误报 Pass，整体状态可解释 | 三态结果；推荐项目原生策略为 invalid → Indeterminate，生产线是否把它汇总成 Fail 由 Safety/Product Policy 显式选择 | Core | LimitEvaluator | 绑定 `analysis_publication_id + trace_evaluation_snapshot_id + trace/pipeline/limit revision`；来源由 typed AnalysisInputRefSet 追溯 | NaN、overload、缺点、部分区间无效 | 各质量标志与四种 Source Ref 的判定矩阵 | 待产品确认 |
| LIM-06 | 失败明细 | 官方至少支持总判定及失败点/刺激明细；margin、失败区间和最坏点是项目 E3 扩展 | 完整结构化 report，SCPI 可分块/二进制查询，并在 schema 中区分直接判定字段与派生摘要 | Core | LimitResultSnapshot | 全部来自同一全分辨率输入 | 大量失败点、margin 并列、报告过大 | 构造多区间失败并校验报告 | 已由证据定案 |
| LIM-07 | 聚合状态 | Channel/Instrument 状态只反映指定兼容测试代次 | 聚合固定 `aggregation_policy_revision + ordered analysis_publication_ids[] + input_generation_vector`，并保存每条结果的 trace/limit revision，不混入陈旧 Fail | Core | Status Aggregator | source-specific synchronization policy 验证输入一致性；全 Live 可要求同一 B 层 ID，Condition/Event/锁存由冻结的 SCPI Profile revision 规定 | 新 Sweep 正在进行、Limit 刚修改、部分 Trace 无结果、Frozen/Imported/Derived 混合 | 连续 Sweep、同 B 层不同 C revision、跨 B 层拒绝及非 Live typed input 聚合测试 | 待兼容目标 |
| LIM-08 | 导入导出与版本 | Limit 表可复用且判定结果能追溯定义版本 | 独立 schema，保存名称、单位、作者、时间、revision | Core | ExchangeFileStore | 导入形成候选定义，验证后 commit | 单位未知、重复段、旧 schema、恶意大表 | round-trip、schema、fuzz 测试 | 已由证据定案 |
| LIM-09 | Web/SCPI 同一判定 | 页面颜色、SCPI fail query 和状态寄存器不会互相矛盾 | 只调用同一 LimitResultSnapshot | Core | Query/Event adapters | Web overlay 由同一定义投影，不另算 | 页面仍显示旧结果时必须标 snapshot/time | 三入口结果 ID/值一致性测试 | 已由证据定案 |
| LIM-10 | 专业模板判定 | Ripple、peak、margin/mask、连续 N 次等不挤占普通 Limit 质量 | 列为独立 Pro evaluator，不塞进 Segment 条件分支 | Pro | Analysis Extension | 每类声明输入 stage 和 accumulator | 未安装时 capability error，不返回空通过 | Pro profile 开关和专用黄金测试 | 待产品确认 |

## Memory、Math、Smoothing、Hold 与 Statistics

| ID | 功能项 | 商用级外部行为 | 当前建议 | 等级 | 架构落点 | 数据阶段/重算语义 | 关键异常/边界 | 验收证据 | 状态 |
|---|---|---|---|---|---|---|---|---|---|
| MATH-01 | Data→Memory | 保存后 Memory 不随新 Sweep 改变，并保留轴、单位和来源 | MemoryTraceSnapshot 为不可变复数快照 | Core | AnalysisCatalog | 默认在 complex trace math 输入阶段捕获 | 无正式数据、存储不足、源快照回收 | 连续扫频后 Memory hash 不变 | 已由证据定案 |
| MATH-02 | Data/Memory 运算 | 支持 Data、Memory、`+ - * /`，运算顺序与商用品一致 | 复数运算在 display formatting 前 | Core | Typed Processing Graph | 输出新 complex node，可继续格式化/分析 | 除零、单位不兼容、Memory 缺失 | 复数黄金数组和错误传播测试 | 已由证据定案 |
| MATH-03 | 轴匹配 | 不同轴不能按下标误算 | 默认 Exact；可显式允许覆盖内插值，禁止静默外推 | Core | Axis Alignment node | 插值策略和质量进入 provenance | 重复/非单调轴、segment gap、覆盖不足 | 多轴 exact/interpolate/reject 表 | 待兼容目标 |
| MATH-04 | Frozen/Reference Trace | 静态比较曲线可显示但不等同于 Math Memory | 独立 FrozenTraceSnapshot，可跨 Workspace 恢复 | Core | AnalysisCatalog / Presentation | 默认进入 Hold，不参与 Math 除非显式导入 | 源 Measurement 已删除、单位/轴不同 | State 恢复后不被 Continuous 覆盖 | 已由证据定案 |
| MATH-05 | Smoothing | aperture、边界和数据阶段明确，不在 invalid gap 上偷偷连线 | 独立 typed node；complex/RI/formatted scalar 由 Profile 选定 | Core | Processing Graph | 修改后 last-good 重算 | 点数太少、边缘窗口、gap、phase wrap | impulse/noise 黄金数组与厂商兼容边界 | 待兼容目标 |
| MATH-06 | Min/Max Hold | 多 Sweep 保留极值，清除和重置时机可预测 | Hold 对声明的 formatted scalar/metric 操作；不定义“复数最大” | Core | TraceAccumulator | 只消费成功完整 Sweep；每次更新/clear 产生不可变 AccumulatorSnapshot，C provenance 固定 snapshot/clear generation/input vector；失败不增加 generation | format/axis/pipeline 改变时 reset 还是 reject | 多 Sweep 极值、clear 前后 single-flight 不复用、失败 Sweep 测试 | 待兼容目标 |
| MATH-07 | 沿 X 统计 | 在全跨度/区间返回 mean/stddev/p-p/min/max | 单 Sweep RangeStatistics 类型 | Core | Statistics Evaluator | 消费声明的标量 projection | 范围边界、invalid 点、空区间、单位 | 已知数组及区间黄金测试 | 待兼容目标 |
| MATH-08 | 跨 Sweep 统计 | 同一点多次扫描统计与沿 X 统计不混淆 | EnsembleStatistics 独立 accumulator | Core/Pro | TraceAccumulator | 仅兼容轴/pipeline revision 进入同组；AnalysisInputRefSet 固定 AccumulatorSnapshot/clear generation/input vector | 缺样、重置、配置变化、持续内存 | 固定随机种子 Mock 多 Sweep 统计；publication provenance 可重放 | 待产品确认 |
| MATH-09 | 统计显示/查询 | 统计结果可通过 Web/SCPI 同代读取并指明样本数 | AnalysisPublication 附 result + valid sample count | Core | Query adapters | 失败 Sweep 不更新 | 样本不足、不确定状态、累加器清除 | Web/SCPI 值、count、generation 一致 | 已由证据定案 |
| MATH-10 | Equation Editor | 高级跨 Trace/Channel 数学不污染 Core 固定运算 | 有类型表达式图列为 Pro，禁止任意脚本进入 RTOS | Pro | Processing Extension | 静态验证轴、单位、循环和内存预算 | 引用循环、表达式爆炸、跨代次 | type checker/fuzz/资源预算测试 | 待产品确认 |

## 参考面、夹具、混模与时域

| ID | 功能项 | 商用级外部行为 | 当前建议 | 等级 | 架构落点 | 数据阶段/重算语义 | 关键异常/边界 | 验收证据 | 状态 |
|---|---|---|---|---|---|---|---|---|---|
| NET-01 | Trace Electrical Delay | 调相显示功能与移动校准参考面是两件事 | Trace delay 是分析投影节点，不改完整 S matrix | Core | Trace Processing | 可基于 last-good 重算 | phase convention、单位、group delay 交互 | 已知线性相位黄金测试 | 待算法/计量验证 |
| NET-02 | Port Extension | 按端口移动参考面，影响涉及该端口的所有 Sij | 对完整 S matrix 使用端口传播算子 | Core | Network Processing node | 在 corrected network 上按 Profile 放置 | 正负 delay、reflection 往返、through 两端、loss/velocity | 已知理想传输线四参数黄金测试 | 待算法/计量验证 |
| NET-03 | Reference Impedance/Renormalization | Z0 转换保持网络数学一致并进入元数据 | 独立矩阵节点；推荐 Core 支持逐端口正实数 Z0 和固定 wave convention，Pro 再开放 complex Z0、balanced Z0 与可选 wave theory | Core/Pro | Network Processing node | 不能用逐 Trace 比例代替 | 奇异点、被动性、各端口不同 Z0、pseudo/power wave | 与参考算法/黄金 Touchstone 比对 | 待产品确认 |
| NET-04 | Reference Plane 元数据 | 用户知道校准面、端口延伸面、Fixture 后 DUT 面 | 每节点显式 input/output plane ID | Core | Typed Processing Graph | 每份结果保存 plane chain | 同名平面、端口重排、链断裂 | provenance 和 UI/SCPI plane query 测试 | 已由证据定案 |
| NET-05 | Fixture 导入 | s2p/sNp 的端口、方向、Z0、频率覆盖可验证 | 文件内容 hash + immutable FixtureRevision | Pro | Fixture Catalog | 后来覆盖原文件不改变历史结果 | 坏 Touchstone、端口数错、覆盖不足、单位错误 | fixture round-trip 与 hash 追溯 | 已由证据定案 |
| NET-06 | Embedding/De-embedding | 按完整网络矩阵级联/反演，不逐 Trace 处理 | typed matrix node；2-port 与 N-port 算法分开 | Pro | vna-compute Fixture module | 消费完整 corrected/processed matrix | 病态/奇异、Z0 不同、端口方向、频率插值 | 合成 DUT×fixture 正反向恢复黄金测试 | 待算法/计量验证 |
| NET-07 | Conditioning/质量 | 数值病态时不输出巨大数仍标 valid | 每点 condition metric + `ill_conditioned` quality | Pro | Processing Quality | 相关输出同频点按依赖失效 | 阈值选择、噪声放大、近奇异矩阵 | sweep condition number 跨阈值测试 | 待算法/计量验证 |
| NET-08 | Fixture 顺序 | Port extension、Z conversion、fixture、mixed-mode 没有伪装成全局唯一顺序 | Product/CompatibilityProfile 给默认合法图，节点类型约束连接 | Pro | Typed Processing Graph validator | 图 revision 进入结果 provenance | 用户重排导致 topology/plane/Z0 不兼容 | 合法/非法图静态验证与厂商 profile 回归 | 待兼容目标 |
| NET-09 | Mixed-mode | 由完整单端矩阵生成 differential/common-mode 参数 | 声明 balanced pair、+/−方向、Z0 和 wave definition | Pro | MixedModeTransform | 与 physical/logical port mapping 分离 | 缺 Sij、pair 重叠、方向反、fixture 位于混模侧 | 已知 4-port 单端↔mixed-mode 黄金矩阵 | 待底软/硬件确认 |
| TD-01 | Time Transform 类型 | 支持 band-pass impulse、low-pass impulse/step，输出明确 time/distance 轴 | TimeDomainTransform 作为 Pro typed node | Pro | Time-domain module | 输出时域 Trace，可独立 Marker/Limit/保存 | 输入类型、轴单位和 transform convention | 理想延迟线/反射的时域峰位置黄金测试 | 待算法/计量验证 |
| TD-02 | 频率网格要求 | 不满足算法网格时明确拒绝或显式重采样 | 默认线性等间隔；low-pass 要 harmonic grid/DC policy | Pro | Axis Validator/Resampler | 重采样产生 `imputed_input` provenance | Log/Segmented、缺点、频率误差容差、重复点 | 各网格 accept/reject/resample 测试 | 待算法/计量验证 |
| TD-03 | Window/Normalization | 窗函数选择对旁瓣/分辨率影响可见且可复现 | 内部保存精确 window family、参数、coherent gain/normalization 与实现 revision；Minimum/Normal/Maximum 只作为 Profile preset | Pro | TimeDomainProfile | 修改基于 same frequency snapshot 重算 | 非法参数、不同点数、幅度归一化 | 窗函数频响/能量黄金测试 | 待算法/计量验证 |
| TD-04 | Zero Padding/范围 | time resolution、alias-free range、点数和距离换算可查询 | metadata 保存 padding、Δt、range、velocity、one-way/round-trip | Pro | Time Axis model | 显示和 Marker 使用同一轴 | velocity 单位、负时域、wrap-around | 已知采样参数的轴公式测试 | 待算法/计量验证 |
| TD-05 | Gate 定义 | band-pass/notch gate 有 start/stop 或 center/span 与 shape | GateDefinition 独立版本化 | Pro | Gate node | 只消费 TimeDomainTransform 的兼容输出 | 越界、反向区间、多个 gate、边缘 taper | 合成双反射分离与 gate shape 测试 | 待算法/计量验证 |
| TD-06 | Gated Frequency Result | Gating 后产生新的频域复数结果，不只是图上遮罩 | 处理图以 `Time → Gate → Inverse` 对单条复数 AnalysisTrace 输出 `GatedFrequencyTraceSnapshot`；完整 S-matrix 一致门控另列 N-port Pro 节点 | Pro | FrequencyDomainGate | 可继续格式化、Marker、Limit 和单 Trace 数据导出；不得冒充完整网络 Touchstone | inverse normalization、DC、缺点、因果/泄漏、多 Sij gate 一致性 | 移除指定反射后的单 Trace 频域黄金结果；N-port 节点另做矩阵一致性测试 | 待算法/计量验证 |
| TD-07 | Time-domain 数据质量 | 一个缺点可能影响全体 FFT 输出，不能按逐点规则掩盖 | 默认整体拒绝；显式插补才允许并标全局质量 | Pro | Validity Policy | quality 非局部传播 | invalid gap、overload、噪声、插值算法 | 单缺点导致 reject/imputed 两 profile 测试 | 待算法/计量验证 |
| NET-10 | 高级 AFR/TDR/眼图 | 不把高端选件冒充基础 FFT/fixture 功能 | AFR、enhanced TDR、eye/mask 列 HW/Option 或独立 Pro | HW/Option | Static Extension | 未安装/不支持时明确 capability error | Mock 有算法不等于真实硬件计量有效 | profile gating 与无空实现测试 | 待产品确认 |

## 尚未闭合的责任门禁

1. **算法/计量（17 项）**：Cartesian/Smith/Polar、Marker 极值/交点/带宽、格式/单位转换、Electrical Delay、Port Extension、fixture cascade/inverse/conditioning，以及整组时域 transform/grid/DC/window/axis/gate/inverse。必须通过本分册所列黄金数组、已知网络和商用 VNA 对照，不能用“界面有结果”验收。
2. **兼容 Profile（11 项）**：对象删除、Marker 插值/Next/peak 条件、Limit 端点与状态锁存、Memory 轴对齐、Smoothing/Hold/Statistics 的阶段与 reset 边界、network node 默认顺序。项目核心保持类型化、确定性默认；厂商副作用只进入 Profile。
3. **真实单板（1 项）**：Mixed-mode 只有在 Board Adapter 能交付同代完整相关 S-matrix、明确端口配对/Z0/wave convention 时才能启用；缺失项不得补零。
4. **真正产品范围（6 项）**：invalid Limit 汇总政策、专业 Limit 模板、跨 Sweep statistics、Equation Editor、renormalization 的 Core/Pro 分层，以及 AFR/enhanced TDR/eye/mask 是否成为独立选件。每项已经给出推荐默认和 capability-gated 失败语义。
5. **已经收敛而不再询问用户的主体**：AnalysisTrace/Placement/Diagram 所有权，完整 Marker 与普通 Limit 子系统，Memory/Frozen 分型，基础 Math、Reference Plane provenance、Fixture import，以及基础时域/门控在处理图中的位置。
