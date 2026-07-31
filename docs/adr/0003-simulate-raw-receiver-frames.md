# 仿真后端输出原始接收机帧

正式 Simulation Backend 只输出与真实 Hardware Backend 相同语义的 RawReceiverPayload，而不是直接生成 S 参数或最终显示曲线。Application 协调器提供 FrameContext 与 FrequencyAxis，并将它们和 payload 组装成 RawReceiverFrame。虽然这使仿真实现更复杂，但能让测量合成、校准、去嵌、格式转换、Marker、文件导出和错误处理共享同一条数据链，并使无硬件测试能够验证真实系统的大部分行为。
