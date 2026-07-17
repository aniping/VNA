# 分离分析迹线与 Diagram 呈现

Trace Definition 是独立于显示布局的稳定分析对象，拥有处理投影、Marker、Limit、Memory、Hold 和统计定义；Diagram 只通过 Trace Presentation 引用它并管理坐标轴、样式和 Overlay。删除或调整 Diagram 不删除 Measurement、Trace Definition 或历史结果，同一 Trace 可以同时呈现在多个 Diagram 中，分析配置也可以基于 last-good 正式快照重算而不要求硬件重新扫频。
