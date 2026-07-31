# Web 与 SCPI 统一进入 Instrument Kernel

> 状态：已由证据定案

Web、SCPI、启动恢复和内部自动操作都转换为同一套类型化 Command、Query 与 Operation，并只通过 Instrument Kernel 修改或读取仪器。协议适配器只负责解析、编码、会话状态和错误映射，不复制 Channel、校准、Marker、Limit、资源仲裁或权限规则，从而保证不同控制入口观察到一致的仪器行为。

Web/SCPI 协议边界不暴露内部 revision，也不得由 Adapter 读取当前 revision 后代客户端补入；Kernel 如何在接受时解析并冻结权威状态见 [ADR-0014](0014-hide-revisions-at-protocol-boundaries.md)。

官方控制行为与项目推导边界见 [控制、状态、文件、安全与平台一手证据](../research/official-vna-control-state-platform-evidence.md)。具体 SCPI 命令、副作用和状态位仍由 Compatibility Profile 冻结。
