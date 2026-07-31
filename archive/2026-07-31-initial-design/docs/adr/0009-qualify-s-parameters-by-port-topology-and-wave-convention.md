# 用端口拓扑和接收机波约定定义 S 参数

> 状态：已接受

本产品使用版本化 `LogicalPortTopology` 支持 2 端口或 4 端口配置，并以 `S(receive_port, source_port)` 表达任意端口对，而不是保存写死的二端口 S 参数字段。Mock 或真实 Board Profile 必须先声明逻辑端口到 source state、receiver path 和 `aᵢ/bᵢ` 的映射，并冻结功率波归一化、参考阻抗与板侧预修正边界；只有该约定与实际 Manifest 一致时，Measurement Pipeline 才能把 `bᵢ/aⱼ` 发布为 `CorrectionApplication=Unbound` 的未校准 `S(i,j)`，否则只能发布 Receiver Ratio。这样既不让单板路径编号渗入 VNA 语义，也不把原始比值冒充已校准网络结果。
