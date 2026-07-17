# 采用 Analysis Trace 与 Diagram Placement 的归一化模型

> 状态：已由证据定案

Keysight 公开区分 Measurement 与 Window 中的显示 Trace；R&S/CMT 则把 measured quantity、format、Marker 和 Limit 更多地暴露在一个外部 Trace 下。三家共同证明“可分析结果”和“Diagram/Window 呈现”需要分开，但没有共同公开同一种内部对象模型。

本项目采用 `AnalysisTrace@revision` 作为稳定的用户分析对象：它包含 `TraceSourceSpec`、处理投影、Marker、Limit、Memory、Hold 和统计定义。Live Source 内含无独立身份的 `MeasurementSpec`；Math、Frozen/Memory Snapshot 和 Imported Data 使用其他 Source 变体。这样不再为每个测量量维护一套浅的 `MeasurementDefinition` CRUD、selection 和删除生命周期，Sweep Compiler 仍从所有 Source Spec、校准步骤和导出请求合并真实采集需求。

`Diagram` 只通过 `TracePlacement` 关联 Analysis Trace，并管理坐标轴、样式、缩放和 Overlay。删除 Placement、删除 Analysis Trace、删除 Diagram 和删除 Channel 是不同的核心 Command；删除副作用及最少保留数量由 Product/Compatibility Profile 组合映射。历史快照不因当前布局删除而改写。

该模型是满足官方外部行为的项目架构决策，不是对商用品闭源实现的声明。兼容映射为：Keysight Measurement ≈ Analysis Trace，Window Trace/FEED ≈ Trace Placement；R&S/CMT Trace ≈ Analysis Trace 加 Placement 的复合外观。

证据矩阵见 [商用 VNA 对象、分析与控制行为的一手证据](../research/official-vna-object-and-analysis-evidence.md)。
