# 发布结构完整但质量降级的测量

> 状态：已接受

B 的发布门禁使用 `MeasurementStructuralCompleteness`：全部必需测量量、Board Run、实际轴、端口拓扑、点数和结果形状必须闭合，任一结构缺失都使本轮失败且不发布 B。接收机过载、失锁、分母过小或非有限结果等逐点质量问题不等同于结构缺失；只要结构完整，系统仍原子发布带逐点 `TypedQualityPlane` 和整体质量摘要的 `CompletedMeasurementBundle`，Sweep Operation 完成并允许 `ChannelMeasurementHead` 指向该 Degraded B。这里的 `last_good_b` 指最近一次结构完整、成功发布的结果，不保证所有点有效；后续 Trace、Marker、Limit、导出和协议层必须传播质量，不能从像素或数值占位符猜测有效性。
