# 跨扫频统计：商用仪器与开源实现的证据边界

## 研究范围与证据等级

- 主题：区分 VNA 中五类容易被“统计”一词混淆的能力。
- 范围：给定厂商官方文档，以及三个固定 commit 的开源快照。
- E1（共同事实）是材料直接支持的共同边界；E2（厂商差异）只适用于特定厂商、产品或处理阶段。
- E3（项目决策）：对 MATH-08 的建议，均未确认，不能写成既定需求。
- E4（目标验证）：将来实现后必须通过的可执行检查。
- “本轮未建立正向证据”只限定本轮材料，不表示相应能力从未存在。

## 五类能力

| 能力 | 聚合轴 | 输出 | 与其它能力的边界 |
| --- | --- | --- | --- |
| 当前轨迹 X 范围统计 | active/selected trace 的刺激值区间 | mean、stddev、p-p 等标量 | 不跨 sweep 聚合同一频点 |
| channel/sweep averaging | 同一频点跨连续 sweeps | 平均后的 trace | 不天然输出 stddev、min、max 或样本集 |
| min/max hold | 同一频点跨历史 sweeps | 历史极值 trace | 不计算均值或离散度 |
| per-point cross-sweep ensemble | 同一频点跨明确样本集合 | mean/stddev/min/max/p-p/count trace | 本文调研的目标能力 |
| production qualification | 被测件、限值、判定和记录 | pass/fail 与可追溯记录 | 不等同于任一数学显示功能 |

## 商用产品的已确认事实

### Keysight：Trace Statistics

- 来源：[Keysight Math Operations](https://helpfiles.keysight.com/csg/N52xxB/S4_Collect/Math_Operations.htm)。
- E1：Trace Statistics 的输入是 active data trace，区间可以是 full stimulus range 或 user-defined range。
- E1：页面明确给出 mean、standard deviation 和 peak-to-peak 等结果。
- E1：它沿 active trace 的刺激值/X 轴取样并输出区间标量，不是同一频点跨 sweeps 的 ensemble 统计。

### Keysight：Channel/Sweep Averaging

- 来源：[Keysight PXI VNA Average SCPI](https://helpfiles.keysight.com/csg/pxivna/Programming/GP-IB_Command_Finder/Sense/Average_SCPI.htm)。
- E1：Keysight 明确把 averaging 定义为连续 sweeps 中对应数据点的测量平均。
- E1：average count/factor 控制 sweep 数，输出逐点平均 trace，而不是区间统计标量。
- E2：它与 Trace Statistics 的数据轴不同，且平均结果不足以证明完整 ensemble API。

### Copper Mountain：Trace Statistics

- 来源：[CMT Trace Statistics](https://coppermountaintech.com/help-cmtvna/1-port/trace-statistics.html)。
- E1：CMT 对一条 trace 的完整频率范围或 markers 限定范围计算统计值。
- E1：页面明确给出 mean、standard deviation 和 peak-to-peak。
- E1：这些结果是选定频率区间内的标量，而非每个频点跨 sweeps 的 trace。

### Copper Mountain：Averaging、Hold 与 Formatted Data

- 来源：[CMT Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html)；[CMT Menu Overview](https://coppermountaintech.com/help-cmtvna/1-port/menu-overview.html)。
- E1：CMT 的 sweep/channel averaging 逐频点聚合连续 sweeps，并与 Trace Statistics、hold 分开。
- E2：formatted data 路径还可包含 smoothing 和 hold，不能默认解释为原始 ensemble 输入。
- E2：smoothing 沿 trace 邻点处理，hold 沿历史 sweeps 保留极值，二者聚合轴不同。
- E2：formatted array 或上述功能组合都不等于暴露完整 ensemble API。

### Rohde & Schwarz：ZVL Trace Statistics

- 来源：[R&S ZVL Operating Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_common_library/dl_manuals/dl_user_manual/ZVL_OperatingManual_en_09.pdf)。
- E1：ZVL 在 selected evaluation range 内对 response values 计算统计量。
- E1：结果包括 minimum、maximum、peak-to-peak、mean、standard deviation 和 RMS。
- E1：样本轴是 evaluation range 内的 response values，不能解释为跨 sweep 的逐点 ensemble trace。
- E2：其指标集合更宽，但不改变 range statistics 与 cross-sweep per-point statistics 的边界。

### Anritsu：Averaging Subsystem

- 来源：[Anritsu VNA Programming Manual](https://dl.cdn-anritsu.com/en-us/test-measurement/files/Manuals/Programming-Manual/10580-00319AD.pdf)。
- E2：手册把 sweep-to-sweep averaging 与 max hold 放在 averaging subsystem 中；这是命令组织，不表示数学语义相同。
- E2：sweep-to-sweep averaging 聚合对应频点；max hold 保留对应频点的历史最大值。
- E2：证据不足以证明完整 ensemble API；这只是本轮未建立正向证据，不宣称该能力永远没有。

## 跨厂商结论

- E1：Keysight 与 CMT 明确支持逐点跨 sweeps 的 measurement averaging，并与 Trace Statistics/Hold 分开。
- E1：Keysight、CMT、R&S 的 range statistics 都沿选定 trace 区间聚合并返回标量。
- E1：hold 沿历史 sweeps 保留极值，既不是 range statistics，也不是 averaging。
- E2：厂商可以把功能放入不同菜单、命令子系统或 formatted-data 阶段。
- E2：证据未建立三家厂商共同具备 per-point cross-sweep mean/stddev；不能改写为“从未存在”。

## 开源固定快照的精确证据

### LibreVNA

- 固定 commit `20247625ad5269c8cdf2fb4408be3666e8679041`：[averaging.h 第 10–40 行](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/averaging.h#L10-L40)。
- 源码：[averaging.cpp 第 102–176 行](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/averaging.cpp#L102-L176)。
- 源码：[tracewaterfall.cpp 第 544–568 行](https://github.com/jankae/LibreVNA/blob/20247625ad5269c8cdf2fb4408be3666e8679041/Software/PC_Application/LibreVNA-GUI/Traces/tracewaterfall.cpp#L544-L568)。
- E2：`Averaging` 为每个 `pointNum` 保存复数测量向量队列，超过最近 N 次时丢弃最旧元素。
- E2：`Mode` 只有 `Mean` 与 `Median`；Mean 对对应复数分量求和并除以队列长度。
- E2：Median 按复数幅度排序；偶数样本时取中间两个复数值的算术平均。
- E2：`TraceWaterfall` 以 `maxDataSweeps` 约束显示行历史并在超限时 `pop_front()`；它不是统计 accumulator。
- E2：这些实现未形成同时暴露 mean/stddev/min/max/p-p/count 的完整 API，但这里不否认局部 level/count 状态。

### NanoVNA-Saver 与 NanoVNA-QT

- 固定快照：NanoVNA-Saver `3445a0ab86161f9c886c9d6f215eb57cab9b6f45`；NanoVNA-QT `0aa6ee4e68ade0285755f06c2eab240e2d0beea1`。
- E2：本轮没有取得可引用的精确文件路径与行级证据。
- E2：只记录“本轮未建立完整 Ensemble API 的正向证据”，不扩张为源码证明其不存在。

## Production Qualification 的独立边界

- E1：production qualification 需要被测件身份、配置、限值、判定规则、上下文和可追溯记录。
- E1：range statistics、average、hold 或 ensemble statistics 都只能提供候选测量量。
- E1/E3：数学量绑定限值版本和策略后才参与 pass/fail；MATH-08 不承担最终判定。
- E4：集成验证应证明统计快照可被 qualification 层引用且不会篡改原始事实。

## MATH-08 建议数据流（未确认）

- E3：建议数据流为 `CompletedSweep -> ScalarProjection -> GridValidation -> EnsembleAccumulator`。
- E3：生成不可变 `StatisticsSnapshot` 供 UI、导出和 qualification 消费；输入携带稳定 grid/sweep identity 和点数。
- E3：`ScalarProjection` 首发只接受定义明确的标量派生量，不比较复数大小。
- E3：网格不一致时显式拒绝或新建 ensemble；每个完成 sweep 是不可 coalesce 的 exact contribution。
- E3：若消费者赶不上生产者，应显式背压、拒绝或结束批次，不能静默覆盖中间 sweeps。
- E3：average、hold、smoothing 和 range statistics 继续作为相邻但独立的数据路径。

## MATH-08 首发范围建议（未确认）

- E3：建议定位 MATH-08 Pro，由 Core 提供统计 seam，首发支持 scalar per-point `FiniteBatch`。
- E3：`FiniteBatch` 接收精确 N 个完成 sweeps，完成后冻结结果，复位后开始下一批。
- E3：建议同时支持 bounded cumulative，限制贡献数、计数范围、内存和单 sweep 时间，不保存无界历史。
- E3：达到上限时冻结、结束或显式报错，不能计数回绕或静默丢样本。
- E3：建议使用在线 mean/M2 等有界状态，但 stddev 的总体/样本定义必须先确认。
- E3：`SlidingWindow` 后置：精确淘汰需保留贡献或可逆状态，内存随 `points × window` 增长且语义更复杂。
- E3：首发不包含 covariance，避免多变量对齐和矩阵资源契约扩张。
- E3：首发不包含 complex comparison，避免未定义的复数次序影响 min/max/p-p。
- E3：RTOS 实现必须对点数、批次、累计次数、队列和执行时间设置静态上限。
- E3：以上全部是未确认建议，不能写入需求基线的“已决定”部分。

## 目标验证

- E4：固定网格输入已知 N 个 exact sweeps，逐点核对 count/mean/min/max/p-p/stddev 及公式。
- E4：证明输入队列不会将两个完成 sweeps 合并为一个 latest value。
- E4：证明 `FiniteBatch` 恰好在 N 次贡献后冻结，不混入第 N+1 次。
- E4：证明 reset 清除全部状态，grid identity/点数变化走明确拒绝或新批次路径。
- E4：证明 NaN、Inf、缺点和中止 sweep 的处理规则可观察且可复现。
- E4：证明 bounded cumulative 达上限不回绕/越界/覆盖，并测量 RTOS 最坏资源成本。
- E4：UI 文案明确区分“选定频段统计”与“跨扫频逐点统计”。
- E4：qualification 层用固定限值版本消费快照，并保留 sweep/batch identity。

## 结论

- E1/E2：官方证据确认三类边界，但官方与开源证据未建立三家厂商共同的完整 per-point Ensemble API。
- E2：LibreVNA 固定快照提供了最近 N 次逐点 Mean/Median 和有界显示历史的具体邻近实现。
- E3：MATH-08 Pro、Core seam、FiniteBatch 与 bounded cumulative 仍是待确认建议。
- E3：在确认前不得把 SlidingWindow、covariance 或 complex comparison 纳入首发承诺。
