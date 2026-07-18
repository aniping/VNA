# 使用不可变快照与类型化处理图

> 状态：已由证据定案；数值节点仍受算法/计量门禁

采集、测量、校准后网络、派生迹线和 Marker/Limit 结果都以带版本与来源信息的不可变快照发布；失败或取消只产生 Operation 终态，不覆盖 last-good 结果。参考面、阻抗转换、夹具、混模、时域、门控和 Trace Math 通过类型化处理图组合，每个节点声明数据阶段、轴、端口拓扑、参考面、Z0、有效性传播和 Preview 能力，避免将厂商相关处理顺序硬编码成不可验证的单线流水线。

非 C 层 receiver/ratio/corrected/ProcessedNetwork 结果如果尚未存在，由 `MaterializeMeasurementStageOperation` 从 canonical A/B roots 和完整 graph revision 惰性产生 `MeasurementStageSnapshot`；正式 Stage 不串联另一个 Stage 为父，图内 intermediate 只属于可淘汰私有 cache。它不携带 AnalysisTrace、Marker 或 Limit revision。只有逐 Trace pipeline/projection 才通过 `LiveMeasurementInput` 或 `MeasurementStageInput` 进入 `EvaluateTraceOperation` 和 C 层 `AnalysisPublication`，因此 SCPI raw/corrected query 与 Touchstone/全矩阵导出不需要伪造 Trace。

跨厂商处理阶段与差异见 [校准与处理链一手证据](../research/official-vna-calibration-processing-evidence.md)；具体公式、符号、归一化和病态矩阵行为必须通过该文定义的黄金数据验收。
