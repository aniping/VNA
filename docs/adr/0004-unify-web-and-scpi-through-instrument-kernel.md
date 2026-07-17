# Web 与 SCPI 统一进入 Instrument Kernel

Web、SCPI、启动恢复和内部自动操作都转换为同一套类型化 Command、Query 与 Operation，并只通过 Instrument Kernel 修改或读取仪器。协议适配器只负责解析、编码、会话状态和错误映射，不复制 Channel、校准、Marker、Limit、资源仲裁或权限规则，从而保证不同控制入口观察到一致的仪器行为。
