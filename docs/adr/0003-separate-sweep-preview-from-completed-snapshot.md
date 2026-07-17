# 分离扫频预览与完整扫频快照

底软适配器可以按点或分块上送当前扫频，Web 使用这些数据展示扫频预览；只有一次扫频成功结束后，上层才原子发布绑定扫频标识、采集配置修订号和实际激励轴的不可变完整扫频快照。校准求解、标记计算、保存和 SCPI 正式数据查询只消费完整快照，失败或取消的预览不得提升为正式结果，远程调用通过 `*WAI`、`*OPC`、`*OPC?` 或状态事件等待具体扫频完成。这与商用 VNA 将异步扫频启动和有效结果完成明确分开的行为一致，参见 [Keysight 命令同步说明](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)。
