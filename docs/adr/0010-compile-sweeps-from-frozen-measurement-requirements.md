# 按冻结测量需求编译逻辑扫频

> 状态：已接受

普通 Channel Sweep 不固定执行完整 N×N 矩阵，也不按 Analysis Trace 分别调用单板；Control 层从同一 Channel revision 冻结全部 Live `MeasurementSpec` 及校准、导出或诊断等显式附加需求，合并去重为 `FrozenMeasurementRequirementSet`，再由 Sweep Compiler 生成最少但完整的 source states 和 receiver observations。完整二端口或四端口矩阵只有在需求集明确包含全部 4 个或 16 个 `S(i,j)` 时才执行；无论集合大小，本轮所需采集和测量结果必须全有或全无地形成一个 `CompletedMeasurementBundle`，任一必需项失败都不发布部分 B，也不替换 last-good B。
