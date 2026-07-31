# 分离扫频预览与完整扫频快照

> 状态：已接受

底软适配器可以按点或分块上送当前扫频，Web 使用这些数据展示扫频预览；只有一次逻辑扫频成功结束后，上层才原子发布绑定 `LogicalSweepId + BoardRunEvidence[]`、parent Manifest set、配置 revision 和实际激励轴的 A 层 `CompletedSweepBundle`，默认单板时 evidence 数组长度为 1。测量处理再发布 B 层 `CompletedMeasurementBundle`，非 Trace 的 receiver/network 分支可以按需发布 `MeasurementStageSnapshot`，每条 Analysis Trace 的正式求值发布 C 层 `AnalysisPublication`。校准标准件采集只引用本次 Attempt 的 A，或 canonical roots 恰为该 A 且被 `CalibrationMethodSpec` 许可的 Stage，以此形成正式 `CalibrationObservationSnapshot`；Marker、Limit、保存和 SCPI 数据查询按命令语义固定 A/B/Stage/C 的类型化正式快照，绝不读取 Preview。失败或取消的预览不得晋升为任何正式层，远程调用通过 `*WAI`、`*OPC`、`*OPC?` 或状态事件等待 Compatibility Profile 明确的 Operation fence。具体所有权与查询闭包见 [端到端数据流契约](../design/data-flow.md)。这与商用 VNA 将异步扫频启动和有效结果完成明确分开的行为一致，参见 [Keysight 命令同步说明](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)。
