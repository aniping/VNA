# 分离单次发布判定与跨 Sweep 生产资格策略

> 状态：已由产品决策定案

普通 upper/lower Segment Limit、Ripple、明确的 Metric threshold 和 Circle 等几何检查是不同的单次发布 evaluator。它们消费同一冻结 `AnalysisInputRefSet` 上的全分辨率 `TraceEvaluationSnapshot`，或消费该候选批有向无环图中显式声明的 typed metric 上游，产生有类型的原始结果；这些结果与 Trace/Marker 在一个 `AnalysisPublication` 中原子发布，并在提交时绑定最终 publication ID。Ripple 是 Core 基础能力，Flatness 保持 Marker/Metric，Circle 只按明确坐标域建模；Eye Mask 仍属于时域眼图应用，不能塞入通用 Analysis Trace evaluator。禁止用万能 `Mask` 或 `LimitSegment` 条件分支兜底。一次发布内需要组合多个判定时，由无状态聚合器引用这些原始结果，不能在 Diagram 或协议 Adapter 中重新计算。

连续 N 次、锁存、bin、批次和 QMS 属于独立 `ProductionQualificationPolicy`。它按参与 ordinal 严格消费已提交的原始结果以及前一策略状态，发布不可变 `ProductionQualificationSnapshot`；策略结果不得回写 `AnalysisPublication`、单次 evaluator result 或其 `Pass / Fail / Indeterminate`。策略必须冻结允许参与的 source、被测件/序列上下文、三态转移和重置规则；策略或上下文不相容时开启新序列，不能沿用旧计数。

普通 Continuous 的显示型 C 仍可按既有 latest-wins 规则合并。生产资格策略一旦武装，每个参与轮次所需的原始 evaluator bundle 就成为不可合并的必达后继；提交 raw C 的同一事务还必须把该 bundle 及其 ordinal 追加到有界 `ProductionQualificationSequence`。策略只处理队首；成功提交新 Snapshot 时才在同一事务消费队首并推进 cursor。求值或提交失败保留队首和上一有效生产结果，后续 raw 可以有界排队但不能越过它；系统以相同 policy/context/ordinal/raw/prior-state 幂等重试。重试预算耗尽时序列进入 `Faulted`。同一序列不得静默跳过缺失轮次；显式 reset 或开启新序列必须把未消费 entry 以可审计的 `AbandonedByReset` 原子终结并释放 pin，失败则原序列完整保留。队列容量不足时在参与工作开始前拒绝或 Hold；Live 输入最迟在 RF start 前只预留轻量 capacity token，不提前执行分析，也不得先扫描再丢结果。

原始单次结果和派生生产结果使用两种不同 typed target；Web 与 SCPI 对同一类 target 必须读取同一事实。Adapter 不得用生产 Fail 覆盖原始 Indeterminate，也不得把原始 Pass 冒充生产放行。Handler、继电器、告警和报告必须显式声明消费哪一种结果。Core/Pro/HW 授权只决定某个 evaluator 或生产策略能否创建和启用，不改变输入、状态转移或结果语义；具体版本与选件组合仍由 `PLAT-12` 决定。

本决策扩展 [ADR-0006](0006-use-immutable-snapshots-and-typed-processing-graph.md) 的不可变发布边界，并服从 [ADR-0013](0013-publish-structurally-complete-degraded-measurements.md) 的质量与 fail-safe 不变量。商用和开源证据及其可推导范围见[高级 Limit 研究](../research/advanced-limit-commercial-open-source-evidence.md)。
