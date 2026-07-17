# 分离扫频预览与完整扫频快照

> 状态：已接受

底软适配器可以按点或分块上送当前扫频，Web 使用这些数据展示扫频预览；只有一次逻辑扫频成功结束后，上层才原子发布绑定 Sweep、Prepared Manifest、配置 revision 和实际激励轴的 A 层 `CompletedSweepBundle`。测量处理再发布 B 层 `CompletedMeasurementBundle`，每条 Analysis Trace 的正式求值发布 C 层 `AnalysisPublication`。校准标准件采集只引用 A 层正式结果；Marker、Limit、保存和 SCPI 数据查询按命令语义固定 B/C 层正式快照，绝不读取 Preview。失败或取消的预览不得晋升为任何正式层，远程调用通过 `*WAI`、`*OPC`、`*OPC?` 或状态事件等待 Compatibility Profile 明确的 Operation fence。这与商用 VNA 将异步扫频启动和有效结果完成明确分开的行为一致，参见 [Keysight 命令同步说明](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)。
